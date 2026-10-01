// Pure helpers for HyprRipple. Imported from QML as `import "Model.js" as Model`
// and ES5-only so the same file runs under node (tests/model.test.js).
//
// ~/.config/hypr/hyprripple.lua is the one place settings live: the panel
// renders it, Hyprland reads it on every reload, and a hand edit is read back
// by parseLua. Live changes go through `hyprctl hyprripple set <key> <value>`
// so nothing waits on a reload.

var LUA_HEADER = "-- HyprRipple: Material Design ink ripples (jgarza.hyprripple)."

// Order here is the order rows appear in the panel and keys in the file.
var SCHEMA = [
  { section: "Triggers", key: "on_click", type: "enum", label: "Mouse clicks",
    description: "Ripple where you press a mouse button.",
    options: [
      { value: "none", label: "Off" },
      { value: "left", label: "Left" },
      { value: "any", label: "Any button" }
    ], fallback: "left" },
  { section: "Triggers", key: "on_focus", type: "bool", label: "Window focus",
    description: "Ripple across a window when it takes focus from a keybind, a new window or a closed one.", fallback: true },
  { section: "Triggers", key: "focus_hover", type: "bool", label: "Hover focus",
    description: "Also ripple when focus follows the mouse. Busy with follow_mouse = 1.", fallback: false,
    needs: "on_focus", needsValue: "true" },
  { section: "Triggers", key: "on_workspace", type: "bool", label: "Workspace switch",
    description: "Flood the screen with ink when you change workspace.", fallback: true },

  { section: "Look", key: "color", type: "color", label: "Color",
    description: "A theme color (accent, foreground, color4, ...) or #rrggbb. Theme colors follow theme switches.",
    options: [
      { value: "accent", label: "Accent" },
      { value: "foreground", label: "Foreground" },
      { value: "selection_background", label: "Selection" }
    ], fallback: "accent" },
  { section: "Look", key: "style", type: "enum", label: "Style",
    description: "A filled ink disc, or an expanding ring.",
    options: [
      { value: "fill", label: "Ink" },
      { value: "ring", label: "Ring" }
    ], fallback: "fill" },
  { section: "Look", key: "opacity", type: "float", label: "Opacity",
    description: "Peak opacity of the ink. Material uses about 0.12 to 0.3.",
    min: 0.05, max: 1, step: 0.01, decimals: 2, fallback: 0.28 },
  { section: "Look", key: "radius", type: "int", label: "Click radius",
    description: "How far a click ripple spreads.", unit: "px",
    min: 20, max: 600, step: 5, fallback: 140 },
  { section: "Look", key: "softness", type: "float", label: "Edge softness",
    description: "Feathering on the ink's edge.", unit: "px",
    min: 0, max: 40, step: 0.5, decimals: 1, fallback: 2 },
  { section: "Look", key: "ring_width", type: "int", label: "Ring width",
    description: "Thickness of the ring style.", unit: "px",
    min: 1, max: 60, step: 1, fallback: 6, needs: "style", needsValue: "ring" },

  { section: "Motion", key: "duration", type: "int", label: "Duration",
    description: "Life of one ripple, spread and fade.", unit: "ms",
    min: 150, max: 2000, step: 25, fallback: 550 },
  { section: "Motion", key: "focus_origin", type: "enum", label: "Focus origin",
    description: "Where a focus ripple starts.",
    options: [
      { value: "cursor", label: "Cursor" },
      { value: "center", label: "Center" }
    ], fallback: "cursor" },
  { section: "Motion", key: "bounded", type: "bool", label: "Clip to window",
    description: "Keep focus ripples inside the window, like Material's bounded ripple.", fallback: true },
  { section: "Motion", key: "workspace_radius", type: "string", label: "Workspace radius",
    description: "'screen' floods the monitor; a number is a fixed radius in px.", fallback: "screen" },
  { section: "Motion", key: "max_ripples", type: "int", label: "Max ripples",
    description: "Older ripples are dropped past this many.",
    min: 1, max: 64, step: 1, fallback: 16 },

  { section: "Triggers", key: "enabled", type: "bool", label: "Enabled",
    description: "Master switch for every trigger.", fallback: true }
]

var SECTIONS = ["Triggers", "Look", "Motion"]

function itemFor(key) {
  for (var i = 0; i < SCHEMA.length; i++)
    if (SCHEMA[i].key === key) return SCHEMA[i]
  return null
}

function defaults() {
  var out = {}
  for (var i = 0; i < SCHEMA.length; i++) out[SCHEMA[i].key] = SCHEMA[i].fallback
  return out
}

// Rows for one section, master switch first, dependent rows hidden when off.
function rowsFor(section, settings) {
  var out = []
  for (var i = 0; i < SCHEMA.length; i++) {
    var item = SCHEMA[i]
    if (item.section !== section) continue
    if (item.needs && settings && String(settings[item.needs]) !== String(item.needsValue)) continue
    if (item.key === "enabled") out.unshift(item)
    else out.push(item)
  }
  return out
}

var HEX_RE = /^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/
var KEY_RE = /^[A-Za-z_][A-Za-z0-9_]*$/

// Validate/normalise one value. Returns { ok, value } or { ok: false, error }.
function coerce(key, raw) {
  var item = itemFor(key)
  if (!item) return { ok: false, error: "unknown setting " + key }

  if (item.type === "bool") {
    if (raw === true || raw === "true" || raw === 1 || raw === "1") return { ok: true, value: true }
    if (raw === false || raw === "false" || raw === 0 || raw === "0") return { ok: true, value: false }
    return { ok: false, error: key + " must be true or false" }
  }

  if (item.type === "int" || item.type === "float") {
    var n = Number(raw)
    if (raw === "" || raw === null || !isFinite(n)) return { ok: false, error: key + " must be a number" }
    n = Math.max(item.min, Math.min(item.max, n))
    if (item.type === "int") n = Math.round(n)
    else n = parseFloat(n.toFixed(item.decimals === undefined ? 2 : item.decimals))
    return { ok: true, value: n }
  }

  if (item.type === "enum") {
    for (var i = 0; i < item.options.length; i++)
      if (item.options[i].value === raw) return { ok: true, value: raw }
    return { ok: false, error: key + " must be one of " + item.options.map(function(o) { return o.value }).join(", ") }
  }

  var s = String(raw === undefined || raw === null ? "" : raw).trim()

  if (item.type === "color") {
    if (s === "") return { ok: true, value: "accent" }
    if (HEX_RE.test(s) || KEY_RE.test(s)) return { ok: true, value: s }
    return { ok: false, error: "color must be a theme color name or #rrggbb" }
  }

  if (key === "workspace_radius") {
    if (s === "" || s === "screen") return { ok: true, value: "screen" }
    var r = Number(s)
    if (isFinite(r) && r > 0) return { ok: true, value: String(Math.round(r)) }
    return { ok: false, error: "workspace_radius must be 'screen' or a number" }
  }

  return { ok: true, value: s }
}

// Fill gaps with defaults and drop anything invalid.
function normalize(settings) {
  var out = defaults()
  if (!settings) return out
  for (var key in settings) {
    var c = coerce(key, settings[key])
    if (c.ok) out[key] = c.value
  }
  return out
}

// ---------------------------------------------------------------- Lua file

function luaValue(v) {
  if (typeof v === "boolean") return v ? "true" : "false"
  if (typeof v === "number") return String(v)
  return '"' + String(v).replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"'
}

function renderLua(settings) {
  var s = normalize(settings)
  var lines = [
    LUA_HEADER,
    "-- Written by the HyprRipple panel: omarchy-shell shell toggle jgarza.hyprripple",
    "-- Hand edits are fine - the panel reads this file back. Keep one key per line.",
    "-- Live, without a reload: hyprctl hyprripple set <key> <value>",
    "",
    "if hl.plugin.hyprripple then",
    "  hl.config({ plugin = { hyprripple = {"
  ]
  for (var i = 0; i < SCHEMA.length; i++) {
    var key = SCHEMA[i].key
    lines.push("    " + key + " = " + luaValue(s[key]) + ",")
  }
  lines.push("  } } })")
  lines.push("end")
  lines.push("")
  return lines.join("\n")
}

// Read `key = value` lines back out of hyprripple.lua. Anything that isn't a
// known key with a valid value is ignored, so a broken hand edit can't take
// the panel down with it.
function parseLua(text) {
  var out = {}
  var lines = String(text || "").split("\n")
  for (var i = 0; i < lines.length; i++) {
    var line = lines[i].replace(/--.*$/, "")
    var m = line.match(/^\s*([a-z_]+)\s*=\s*("(?:[^"\\]|\\.)*"|'[^']*'|true|false|-?[0-9.]+)\s*,?\s*$/)
    if (!m || !itemFor(m[1])) continue
    var raw = m[2]
    var value
    if (raw === "true") value = true
    else if (raw === "false") value = false
    else if (raw.charAt(0) === '"') value = raw.slice(1, -1).replace(/\\(.)/g, "$1")
    else if (raw.charAt(0) === "'") value = raw.slice(1, -1)
    else value = Number(raw)
    var c = coerce(m[1], value)
    if (c.ok) out[m[1]] = c.value
  }
  return normalize(out)
}

// ------------------------------------------------------------- hyprctl

function setCommand(key, value) {
  var v = typeof value === "boolean" ? (value ? "true" : "false") : String(value)
  return ["hyprctl", "hyprripple", "set", key, v]
}

// The panel pushes every key at once on open/service start so a plugin that
// loaded before the Lua file existed still matches it. One hyprctl call.
function batchCommand(settings) {
  var s = normalize(settings)
  var parts = []
  for (var i = 0; i < SCHEMA.length; i++) {
    var key = SCHEMA[i].key
    var v = typeof s[key] === "boolean" ? (s[key] ? "true" : "false") : String(s[key])
    parts.push("hyprripple set " + key + " " + v)
  }
  return ["hyprctl", "--batch", parts.join(" ; ")]
}

// `hyprctl hyprripple get` -> settings, or null if the plugin isn't loaded.
function parseStatus(text) {
  try {
    var obj = JSON.parse(String(text || "").trim())
    if (!obj || typeof obj !== "object" || obj.enabled === undefined) return null
    return obj
  } catch (e) {
    return null
  }
}

if (typeof module !== "undefined") {
  module.exports = {
    SCHEMA: SCHEMA,
    SECTIONS: SECTIONS,
    itemFor: itemFor,
    defaults: defaults,
    rowsFor: rowsFor,
    coerce: coerce,
    normalize: normalize,
    renderLua: renderLua,
    parseLua: parseLua,
    setCommand: setCommand,
    batchCommand: batchCommand,
    parseStatus: parseStatus
  }
}
