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
  on_click: "any", on_focus: false, color: "#12abef", opacity: 0.4, style: "ring", workspace_radius: "1200",
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

// hyprctl
ok("set command bool", M.setCommand("bounded", false).join(" ") === "hyprctl hyprripple set bounded false");
const batch = M.batchCommand(M.defaults());
ok("batch is one call", batch[0] === "hyprctl" && batch[1] === "--batch");
ok("batch has every key", M.SCHEMA.every((i) => batch[2].includes("hyprripple set " + i.key + " ")));
ok("status parses", M.parseStatus('{"enabled":true,"opacity":0.3}').opacity === 0.3);
ok("status rejects junk", M.parseStatus("unknown request") === null);

console.log(failed ? `\n${failed} failed` : "\nall passed");
process.exit(failed ? 1 : 0);
