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
  { section: "Triggers", key: "on_touch", type: "bool", label: "Touch & pen",
    description: "Ripple where a finger or tablet pen touches down.", fallback: true },
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
  { section: "Look", key: "style", type: "enum", label: "Preset",
    description: "Ink: Classic, Ring, Sparkle. Screen effects: Negative inverts, Water refracts in a travelling wave, Prism splits into a rainbow shockwave (Opacity = effect strength). Each remembers its own opacity, radius and softness.",
    options: [
      { value: "classic", label: "Classic" },
      { value: "ring", label: "Ring" },
      { value: "sparkle", label: "Sparkle" },
      { value: "negative", label: "Negative" },
      { value: "water", label: "Water" },
      { value: "prism", label: "Prism" }
    ], fallback: "classic" },
  { section: "Look", key: "opacity", type: "float", label: "Opacity",
    description: "Peak opacity of the ink. Material uses about 0.12 to 0.3.",
    min: 0.05, max: 1, step: 0.01, decimals: 2, fallback: 0.28 },
  { section: "Look", key: "radius", type: "int", label: "Click radius",
    description: "How far a click ripple spreads.", unit: "px",
    min: 20, max: 600, step: 5, fallback: 140 },
  { section: "Look", key: "softness", type: "float", label: "Edge softness",
    description: "Feathering on the ink's edge.", unit: "px",
    min: 0, max: 40, step: 0.5, decimals: 1, fallback: 2 },
  { section: "Look", key: "sparkle_amount", type: "float", label: "Sparkle amount",
    description: "How much shimmering grain the sparkle preset carries. 0 is a plain glow.",
    min: 0, max: 1, step: 0.05, decimals: 2, fallback: 0.5, needs: "style", needsValue: "sparkle" },
  { section: "Look", key: "distortion", type: "float", label: "Distortion",
    description: "How hard the wave bends what's under it.",
    min: 0, max: 1, step: 0.05, decimals: 2, fallback: 0.5, needs: "style", needsValue: ["water", "prism"] },
  { section: "Look", key: "ring_width", type: "int", label: "Ring width",
    description: "Thickness of the ring style.", unit: "px",
    min: 1, max: 60, step: 1, fallback: 6, needs: "style", needsValue: "ring" },

  { section: "Motion", key: "hold", type: "bool", label: "Grow while held",
    description: "Like Material: ink keeps spreading while you hold the button and fades when you let go. Off = every ripple runs for Duration.", fallback: true },
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
    description: "Keep click, tap and focus ripples inside their window, like Material's bounded ripple. Clicks on the desktop or bar stay unclipped.", fallback: true },
  { section: "Motion", key: "workspace_radius", type: "string", label: "Workspace radius",
    description: "'screen' floods the monitor; a number is a fixed radius in px.", fallback: "screen" },
  { section: "Motion", key: "max_ripples", type: "int", label: "Max ripples",
    description: "Older ripples are dropped past this many.",
    min: 1, max: 64, step: 1, fallback: 16 },

  { section: "Exclude", key: "skip_fullscreen", type: "bool", label: "Skip fullscreen",
    description: "No ripples on a monitor showing a fullscreen window: games, video.", fallback: true },
  { section: "Exclude", key: "pause_when_sharing", type: "bool", label: "Pause while sharing",
    description: "No ripples while the screen is being shared or recorded.", fallback: false },
  { section: "Exclude", key: "exclude", type: "text", label: "Excluded apps",
    description: "Window classes that never ripple, comma-separated. * and ? wildcards, e.g. steam_app_*, mpv",
    placeholder: "steam_app_*, mpv", fallback: "" },

  { section: "Triggers", key: "enabled", type: "bool", label: "Enabled",
    description: "Master switch for every trigger.", fallback: true }
]

var SECTIONS = ["Triggers", "Look", "Motion", "Exclude"]

// What picking a preset sets alongside `style`. Everything stays tweakable.
var PRESETS = {
  classic: { opacity: 0.28, radius: 140, softness: 2, duration: 550 },
  ring:    { opacity: 0.5, radius: 140, softness: 1.5, ring_width: 6, duration: 600 },
  sparkle: { opacity: 0.2, radius: 140, softness: 2, duration: 800 },
  negative: { opacity: 1, radius: 140, softness: 2, duration: 600 },
  water:    { opacity: 0.9, radius: 220, softness: 2, duration: 1100 },
  prism:    { opacity: 0.9, radius: 240, softness: 2, duration: 900 }
}

// Values each preset remembers on its own: tweak sparkle's opacity, switch to
// classic and back, and sparkle's opacity comes back. Their reset buttons
// return to the current preset's default rather than the global one.
var PRESET_KEYS = ["opacity", "radius", "softness"]

function isPresetKey(key) {
  return PRESET_KEYS.indexOf(key) !== -1
}

// What a row's reset button restores, given the current settings.
function defaultFor(key, settings) {
  var item = itemFor(key)
  if (!item) return undefined
  if (isPresetKey(key) && settings) {
    var preset = PRESETS[settings.style]
    if (preset && preset[key] !== undefined) return preset[key]
  }
  return item.fallback
}

// Record the current preset's remembered values into `memory`
// ({ style: { opacity, radius, softness } }); returns a new object.
function rememberPreset(memory, settings) {
  var out = cleanMemory(memory)
  if (!settings || !PRESETS[settings.style]) return out
  var slot = {}
  for (var i = 0; i < PRESET_KEYS.length; i++) {
    var c = coerce(PRESET_KEYS[i], settings[PRESET_KEYS[i]])
    if (c.ok) slot[PRESET_KEYS[i]] = c.value
  }
  out[settings.style] = slot
  return out
}

// Switch preset: remember the one we leave, apply the new preset's defaults,
// then its remembered values on top. Returns { settings, memory }.
function switchPreset(settings, memory, style) {
  if (!PRESETS[style]) return { settings: settings, memory: cleanMemory(memory) }
  var mem = rememberPreset(memory, settings)
  var next = applyPreset(settings, style)
  var saved = mem[style]
  if (saved)
    for (var i = 0; i < PRESET_KEYS.length; i++)
      if (saved[PRESET_KEYS[i]] !== undefined) next[PRESET_KEYS[i]] = saved[PRESET_KEYS[i]]
  return { settings: next, memory: mem }
}

// Only known presets and keys, with validated values - the file is
// hand-editable, so treat it as untrusted.
function cleanMemory(memory) {
  var out = {}
  if (!memory || typeof memory !== "object") return out
  for (var style in PRESETS) {
    if (!Object.prototype.hasOwnProperty.call(memory, style)) continue
    var slot = memory[style]
    if (!slot || typeof slot !== "object") continue
    var clean = {}
    for (var i = 0; i < PRESET_KEYS.length; i++) {
      var key = PRESET_KEYS[i]
      if (!Object.prototype.hasOwnProperty.call(slot, key)) continue
      var c = coerce(key, slot[key])
      if (c.ok) clean[key] = c.value
    }
    out[style] = clean
  }
  return out
}

// `hyprripplestyle>>old,new` from Hyprland's event socket -> { from, to },
// or null if it isn't two known presets.
function parseStyleEvent(data) {
  var parts = String(data || "").split(",")
  if (parts.length !== 2 || !PRESETS[parts[0]] || !PRESETS[parts[1]] || parts[0] === parts[1]) return null
  return { from: parts[0], to: parts[1] }
}

// The service's half of a style change made anywhere (panel, hyprctl, Lua):
// the plugin already switched `style` but still holds the old preset's
// opacity/radius/softness, so `live` (its `hyprctl hyprripple get`) is
// exactly what to remember for `from`.
function switchFromLive(live, from, to, memory) {
  var settings = normalize(live)
  settings.style = from
  return switchPreset(settings, memory, to)
}

function parseMemory(text) {
  try { return cleanMemory(JSON.parse(String(text || ""))) } catch (e) { return {} }
}

function renderMemory(memory) {
  return JSON.stringify(cleanMemory(memory), null, 2) + "\n"
}

// settings with `style` and its preset values applied
function applyPreset(settings, style) {
  var out = {}
  for (var k in settings) out[k] = settings[k]
  var preset = PRESETS[style]
  if (!preset) return out
  out.style = style
  for (var key in preset) out[key] = preset[key]
  return out
}

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

// A dependent row shows when its `needs` key has `needsValue` - one value or
// any of an array of them.
function needsMet(item, settings) {
  var have = String(settings[item.needs])
  var want = item.needsValue
  if (Array.isArray(want)) {
    for (var i = 0; i < want.length; i++)
      if (String(want[i]) === have) return true
    return false
  }
  return String(want) === have
}

// Rows for one section, master switch first, dependent rows hidden when off.
function rowsFor(section, settings) {
  var out = []
  for (var i = 0; i < SCHEMA.length; i++) {
    var item = SCHEMA[i]
    if (item.section !== section) continue
    if (item.needs && settings && !needsMet(item, settings)) continue
    if (item.key === "enabled") out.unshift(item)
    else out.push(item)
  }
  return out
}

var HEX_RE = /^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/
var KEY_RE = /^[A-Za-z_][A-Za-z0-9_]{0,63}$/
// Same whitelist the native plugin enforces: nothing that could break out of
// a Lua string or chain another command in `hyprctl --batch`.
var EXCLUDE_RE = /^[A-Za-z0-9._\-*?, ]{0,512}$/

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
    if (key === "style" && raw === "fill") raw = "classic" // pre-preset name
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
    if (isFinite(r) && r > 0) return { ok: true, value: String(Math.round(Math.max(10, Math.min(20000, r)))) }
    return { ok: false, error: "workspace_radius must be 'screen' or a number" }
  }

  if (key === "exclude") {
    if (EXCLUDE_RE.test(s)) return { ok: true, value: s }
    return { ok: false, error: "excluded apps: only letters, digits, . _ - * ? and commas" }
  }

  return { ok: false, error: "unsupported setting " + key }
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
  return '"' + String(v)
    .replace(/\\/g, "\\\\")
    .replace(/"/g, '\\"')
    .replace(/\n/g, "\\n")
    .replace(/\r/g, "\\r")
    .replace(/[\x00-\x1f]/g, "") + '"'
}

// Keyboard: what Left (-1) / Right (+1) does to a row's value.
function nudge(item, value, direction) {
  if (!item) return value
  if (item.type === "bool") return direction > 0
  if (item.type === "enum" || item.type === "color") {
    var options = item.options || []
    if (options.length === 0) return value
    var at = -1
    for (var i = 0; i < options.length; i++)
      if (String(options[i].value) === String(value)) at = i
    if (at < 0) return options[direction > 0 ? 0 : options.length - 1].value
    return options[(at + direction + options.length) % options.length].value
  }
  if (item.type === "int" || item.type === "float") {
    var step = item.step === undefined ? 1 : item.step
    var n = Number(value) + step * (direction > 0 ? 1 : -1)
    n = Math.max(item.min, Math.min(item.max, n))
    // keep float steps from drifting (0.1 + 0.2 ...)
    return parseFloat(n.toFixed(item.type === "int" ? 0 : (item.decimals === undefined ? 2 : item.decimals)))
  }
  return value
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
    PRESETS: PRESETS,
    nudge: nudge,
    applyPreset: applyPreset,
    PRESET_KEYS: PRESET_KEYS,
    isPresetKey: isPresetKey,
    defaultFor: defaultFor,
    rememberPreset: rememberPreset,
    switchPreset: switchPreset,
    cleanMemory: cleanMemory,
    parseMemory: parseMemory,
    parseStyleEvent: parseStyleEvent,
    switchFromLive: switchFromLive,
    renderMemory: renderMemory,
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
