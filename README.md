# HyprRipple

Material Design ink ripples drawn by Hyprland itself, in your Omarchy theme's
accent color.

- **Click**: ink spreads from where you press (left button or any button).
- **Window focus**: a bounded ripple floods the newly focused window, clipped to
  its rounded corners. Fires on keyboard focus, new windows and closed windows.
  Hover focus (`follow_mouse`) is opt-in, and starts on the edge the mouse
  crossed.
- Ripples on a window ride along with it: moves, resizes, scrolling-layout
  slides and workspace animations.
- **Workspace switch**: a wave floods the whole monitor from the cursor.

Three presets (`style`). Picking one in the panel also sets its opacity,
softness and duration:

- **classic**: Material 2 ink, a near-solid disc with a feathered rim.
- **ring**: an expanding ring.
- **sparkle**: Material 3 / Android 12. A soft radial glow with a turbulent
  edge and shimmering noise sparkles that trail the wave and dissolve as it
  fades. It uses M3's `cubic-bezier(0.2, 0, 0, 1)` easing.

There are two halves:

| Part | What it is |
|---|---|
| `native/` | A C++ Hyprland plugin (`hyprripple.so`) with a GLSL ripple shader (`native/shaders/ripple.frag`). It renders right after windows, under bars and overlays. |
| `Panel.qml`, `Service.qml` | An Omarchy shell plugin: a settings panel that changes things live, plus a service that keeps the native plugin in sync with `~/.config/hypr/hyprripple.lua`. |

## Install

```sh
hyprpm add ~/.config/omarchy/plugins/jgarza.hyprripple
hyprpm enable hyprripple
hyprpm reload -n
```

`hyprpm reload -n` already runs at login from `autostart.lua`. The config side
is already wired up:

- `~/.config/hypr/hyprripple.lua`
- `require("hypr.hyprripple")` in `~/.config/hypr/hyprland.lua`

Open the settings from the Omarchy menu: search "ripple", or go to Style →
HyprRipple. These entries live in `~/.config/omarchy/extensions/omarchy-menu.jsonc`.
From a terminal:

```sh
omarchy-shell shell toggle jgarza.hyprripple
```

After editing the C++ or the shader, commit and run `hyprpm update`, because
hyprpm builds from the repo's HEAD. For a quick dev loop, use `make` and then
`hyprctl plugin load $PWD/hyprripple.so`, but don't keep it loaded alongside
the hyprpm copy.

## Settings

All settings live in `~/.config/hypr/hyprripple.lua`. The panel writes this
file, and hand edits are picked up live by the service.

| Key | Default | |
|---|---|---|
| `enabled` | `true` | master switch |
| `on_click` | `"left"` | `none` / `left` / `any` |
| `on_focus` | `true` | ripple on focus change |
| `focus_hover` | `false` | also on focus-follows-mouse |
| `on_workspace` | `true` | ripple on workspace switch |
| `color` | `"accent"` | any `colors.toml` key (`accent`, `foreground`, `color4`...) or `#rrggbb[aa]` |
| `style` | `"classic"` | `classic` / `ring` / `sparkle` (`fill` still means classic) |
| `opacity` | `0.28` | peak ink opacity |
| `radius` | `140` | click ripple radius, logical px |
| `softness` | `2` | edge feather, px |
| `ring_width` | `6` | ring thickness, px |
| `duration` | `550` | ms |
| `focus_origin` | `"cursor"` | `cursor` / `center` |
| `bounded` | `true` | clip focus ripples to the window |
| `workspace_radius` | `"screen"` | `screen` or px |
| `max_ripples` | `16` | oldest are dropped past this |

Theme colors are resolved from
`~/.local/state/omarchy/current/theme/colors.toml` each time a ripple
spawns, so a theme switch recolors ripples with no reload.

## hyprctl

```sh
hyprctl hyprripple get                    # current settings (JSON)
hyprctl hyprripple set opacity 0.2        # live, no reload
hyprctl hyprripple trigger [x y [radius]] # ripple now (cursor if no x y)
hyprctl hyprripple stats                  # spawn counters per trigger
```

From Lua, for example in a bind:
`hl.plugin.hyprripple.trigger()` and `hl.plugin.hyprripple.set("style", "ring")`.

Hyprland's Lua parser rejects `hyprctl keyword`, which is why the panel talks
to the plugin's own `hyprripple set` command.

## Tests

```sh
node tests/model.test.js
```
