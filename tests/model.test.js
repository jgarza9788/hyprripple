// Run: node tests/model.test.js
const M = require("../Model.js");

let failed = 0;
function ok(name, cond) {
  console.log((cond ? "PASS " : "FAIL ") + name);
  if (!cond) failed++;
}

// defaults / schema
const d = M.defaults();
ok("defaults cover every key", M.SCHEMA.every((i) => d[i.key] !== undefined));
ok("default color is accent", d.color === "accent");
ok("enabled row leads Triggers", M.rowsFor("Triggers", d)[0].key === "enabled");
ok("ring width hidden for fill", !M.rowsFor("Look", { style: "fill" }).some((i) => i.key === "ring_width"));
ok("ring width shown for ring", M.rowsFor("Look", { style: "ring" }).some((i) => i.key === "ring_width"));

// coerce
ok("bool from string", M.coerce("on_focus", "false").value === false);
ok("bad bool rejected", !M.coerce("on_focus", "maybe").ok);
ok("number clamped high", M.coerce("opacity", 7).value === 1);
ok("number clamped low", M.coerce("radius", 1).value === 20);
ok("int rounded", M.coerce("duration", 333.6).value === 334);
ok("float decimals", M.coerce("opacity", 0.123456).value === 0.12);
ok("non-number rejected", !M.coerce("radius", "big").ok);
ok("enum accepted", M.coerce("on_click", "any").value === "any");
ok("enum rejected", !M.coerce("on_click", "right").ok);
ok("hex color", M.coerce("color", "#ff00aa").value === "#ff00aa");
ok("theme color key", M.coerce("color", "color4").value === "color4");
ok("empty color -> accent", M.coerce("color", "").value === "accent");
ok("junk color rejected", !M.coerce("color", "red; os.exit()").ok);
ok("workspace screen", M.coerce("workspace_radius", "screen").value === "screen");
ok("workspace number", M.coerce("workspace_radius", "900.4").value === "900");
ok("workspace junk rejected", !M.coerce("workspace_radius", "-3").ok);
ok("unknown key rejected", !M.coerce("nope", 1).ok);

// render -> parse round trip
const custom = Object.assign(M.defaults(), {
  on_click: "any", on_focus: false, color: "#12abef", opacity: 0.4, style: "sparkle", workspace_radius: "1200",
});
const lua = M.renderLua(custom);
ok("lua guarded by plugin check", lua.includes("if hl.plugin.hyprripple then"));
ok("lua uses plugin config table", lua.includes("hl.config({ plugin = { hyprripple = {"));
ok("lua bool", lua.includes("on_focus = false,"));
ok("lua string quoted", lua.includes('color = "#12abef",'));
ok("lua number bare", lua.includes("opacity = 0.4,"));
const back = M.parseLua(lua);
ok("round trip", M.SCHEMA.every((i) => back[i.key] === custom[i.key]));

// parse tolerates hand edits
const hand = `
  -- comment
  opacity = 0.5, -- trailing comment
  style = 'ring'
  radius = 99999,
  on_click = "middle",
  bogus = 3,
`;
const p = M.parseLua(hand);
ok("hand edit number", p.opacity === 0.5);
ok("single quotes", p.style === "ring");
ok("out of range clamped", p.radius === 600);
ok("invalid enum falls back", p.on_click === "left");
ok("unknown key ignored", p.bogus === undefined);
ok("empty file -> defaults", JSON.stringify(M.parseLua("")) === JSON.stringify(M.defaults()));

// presets
ok("old fill name maps to classic", M.coerce("style", "fill").value === "classic");
ok("sparkle accepted", M.coerce("style", "sparkle").value === "sparkle");
const sp = M.applyPreset(M.defaults(), "sparkle");
ok("preset sets style", sp.style === "sparkle");
ok("preset sets its values", sp.opacity === M.PRESETS.sparkle.opacity && sp.duration === M.PRESETS.sparkle.duration);
ok("preset keeps triggers", sp.on_click === "left");
ok("preset values are valid", Object.keys(M.PRESETS).every((n) => Object.keys(M.PRESETS[n]).every((k) => M.coerce(k, M.PRESETS[n][k]).value === M.PRESETS[n][k])));
ok("unknown preset is a no-op", M.applyPreset(M.defaults(), "nope").style === "classic");

// new settings
ok("touch default on", d.on_touch === true);
ok("hold default on", d.hold === true);
ok("exclude default empty", d.exclude === "");
ok("Exclude section exists", M.SECTIONS.includes("Exclude") && M.rowsFor("Exclude", d).length === 3);
ok("sparkle amount hidden unless sparkle", !M.rowsFor("Look", { style: "classic" }).some((i) => i.key === "sparkle_amount"));
ok("sparkle amount shown for sparkle", M.rowsFor("Look", { style: "sparkle" }).some((i) => i.key === "sparkle_amount"));
ok("sparkle amount clamps", M.coerce("sparkle_amount", 4).value === 1);
ok("exclude globs ok", M.coerce("exclude", "steam_app_*, mpv").ok);
ok("workspace radius clamps", M.coerce("workspace_radius", "999999").value === "20000");

// security: nothing that could escape the Lua string or chain a hyprctl command
for (const bad of ['a;b', 'a"b', "a\nb", "a\\b", "mpv`id`", "$(id)", "x".repeat(600)])
  ok("exclude rejects " + JSON.stringify(bad).slice(0, 20), !M.coerce("exclude", bad).ok);
for (const bad of ['red;exit', 'a"b', "#12345", "a b", "x".repeat(80)])
  ok("color rejects " + JSON.stringify(bad).slice(0, 20), !M.coerce("color", bad).ok);
ok("workspace rejects injection", !M.coerce("workspace_radius", "1 ; dispatch exit").ok);
ok("unknown string key rejected", !M.coerce("toString", "x").ok && !M.coerce("__proto__", "x").ok);
const evil = M.parseLua('exclude = "x\"; os.execute(\"id\") --",\ncolor = "red;exit",');
ok("parseLua drops injected values", evil.exclude === "" && evil.color === "accent");
const batchEvil = M.batchCommand({ exclude: "a;b", color: "x;y" });
ok("batch never carries ';' from values", batchEvil[2].split(" ; ").length === M.SCHEMA.length);
ok("lua escapes newline", M.renderLua(M.defaults()).split("\n").every((l) => !l.includes("\r")));

// keyboard nudge
const opacity = M.itemFor("opacity");
ok("nudge float up", M.nudge(opacity, 0.28, 1) === 0.29);
ok("nudge float no drift", M.nudge(opacity, 0.1, 1) === 0.11);
ok("nudge clamps max", M.nudge(opacity, 1, 1) === 1);
ok("nudge clamps min", M.nudge(opacity, 0.05, -1) === 0.05);
ok("nudge int", M.nudge(M.itemFor("radius"), 140, -1) === 135);
ok("nudge bool right = on", M.nudge(M.itemFor("on_focus"), false, 1) === true);
ok("nudge bool left = off", M.nudge(M.itemFor("on_focus"), true, -1) === false);
ok("nudge enum cycles", M.nudge(M.itemFor("on_click"), "any", 1) === "none");
ok("nudge enum back", M.nudge(M.itemFor("on_click"), "none", -1) === "any");
ok("nudge color from custom", M.nudge(M.itemFor("color"), "#123456", 1) === "accent");
ok("nudge text no-op", M.nudge(M.itemFor("exclude"), "mpv", 1) === "mpv");

// hyprctl
ok("set command bool", M.setCommand("bounded", false).join(" ") === "hyprctl hyprripple set bounded false");
const batch = M.batchCommand(M.defaults());
ok("batch is one call", batch[0] === "hyprctl" && batch[1] === "--batch");
ok("batch has every key", M.SCHEMA.every((i) => batch[2].includes("hyprripple set " + i.key + " ")));
ok("status parses", M.parseStatus('{"enabled":true,"opacity":0.3}').opacity === 0.3);
ok("status rejects junk", M.parseStatus("unknown request") === null);

console.log(failed ? `\n${failed} failed` : "\nall passed");
process.exit(failed ? 1 : 0);
