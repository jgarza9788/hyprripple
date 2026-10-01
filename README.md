# HyprRipple

Material Design ink ripples drawn by Hyprland itself, in your Omarchy theme's
accent color.

- **Click**: ink spreads from where you press (left button or any button).
  Like Material, it keeps spreading while you hold the button and fades when
  you let go. A quick click is a quick splash.
- **Touch & pen**: the same for touchscreen taps and tablet pens.
- **Bars & launchers**: a press on a top/overlay layer (Waybar, the Omarchy
  menu) ripples above it, clipped to that surface.
- **Window focus**: a bounded ripple floods the newly focused window, clipped to
  its rounded corners. Fires on keyboard focus, new windows and closed windows.
  Hover focus (`follow_mouse`) is opt-in, and starts on the edge the mouse
  crossed.
- Ripples on a window ride along with it: moves, resizes, scrolling-layout
  slides and workspace animations.
- **Workspace switch**: a wave floods the whole monitor from the cursor.

Six presets (`style`). Picking one in the panel also sets its opacity,
softness and duration:

- **classic**: Material 2 ink, a near-solid disc with a feathered rim.
- **ring**: an expanding ring.
- **sparkle**: Material 3 / Android 12. A soft radial glow with a turbulent
  edge and shimmering noise sparkles that trail the wave and dissolve as it
  fades. It uses M3's `cubic-bezier(0.2, 0, 0, 1)` easing.
- **negative**: the screen under the disc, colour-inverted. Holding keeps it
  inverted.
- **water**: the screen refracted by a travelling wave packet, with a faint
  accent caustic on the crest. `distortion` sets how hard it bends.
- **prism**: the same wave, but red, green and blue refract by different
  amounts, giving a rainbow-fringed shockwave with an accent glow.

Negative, water and prism read the frame rendered so far: one GPU copy per
ripple per frame, only while one is alive. They sample in framebuffer pixel
space, so they're correct under any monitor rotation (untested on a rotated
monitor). For these three, Opacity is the effect's strength. Water and prism
are travelling waves, so they ignore hold.

Each preset remembers its own opacity, radius and edge softness. Tweak
sparkle, switch to classic and back, and your sparkle values return. Those
three rows' reset buttons go back to the current preset's default. The panel
keeps this memory in `~/.config/omarchy/jgarza.hyprripple/presets.json`.
This works however the style changes: the panel, `hyprctl hyprripple set
style …`, or Lua. The plugin announces `hyprripplestyle>>old,new` on
Hyprland's event socket, and the background service does the swap.

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
| `on_touch` | `true` | touchscreen / tablet pen taps |
| `hold` | `true` | ink grows while held, fades on release (max 8 s) |
| `on_focus` | `true` | ripple on focus change |
| `focus_hover` | `false` | also on focus-follows-mouse |
| `on_workspace` | `true` | ripple on workspace switch |
| `color` | `"accent"` | any `colors.toml` key (`accent`, `foreground`, `color4`...) or `#rrggbb[aa]` |
| `style` | `"classic"` | `classic` / `ring` / `sparkle` / `negative` / `water` / `prism` (`fill` still means classic) |
| `opacity` | `0.28` | peak ink opacity |
| `radius` | `140` | click ripple radius, logical px |
| `softness` | `2` | edge feather, px |
| `ring_width` | `6` | ring thickness, px |
| `sparkle_amount` | `0.5` | sparkle grain, 0 (plain glow) to 1 |
| `distortion` | `0.5` | water / prism wave strength, 0 to 1 |
| `duration` | `550` | ms |
| `focus_origin` | `"cursor"` | `cursor` / `center` |
| `bounded` | `true` | clip click, tap and focus ripples to their window |
| `workspace_radius` | `"screen"` | `screen` or px |
| `max_ripples` | `16` | oldest are dropped past this |
| `skip_fullscreen` | `true` | no ripples on a monitor showing a fullscreen window |
| `pause_when_sharing` | `false` | no ripples while screen sharing / recording |
| `exclude` | `""` | window classes that never ripple: comma-separated, `*` / `?` globs |

Theme colors are resolved from
`~/.local/state/omarchy/current/theme/colors.toml` each time a ripple
spawns, so a theme switch recolors ripples with no reload.

## hyprctl

```sh
hyprctl hyprripple get                    # current settings (JSON)
hyprctl hyprripple set opacity 0.2        # live, no reload
hyprctl hyprripple trigger [x y [radius]] [above]  # ripple now; "above" draws over bars
hyprctl hyprripple sharing [reset]        # screen-share pause state
hyprctl hyprripple stats                  # counters, frames drawn, live ripples
```

From Lua, for example in a bind:
`hl.plugin.hyprripple.trigger()` and `hl.plugin.hyprripple.set("style", "ring")`.

Hyprland's Lua parser rejects `hyprctl keyword`, which is why the panel talks
to the plugin's own `hyprripple set` command.

## Panel keys

Tab switches section. ↑↓ or j/k move between rows. ←→ or h/l change the value.
Enter/Space toggles, or edits a text field. Backspace resets the row. T fires
a test ripple. Esc closes.

## Tests

```sh
make test   # C++ logic tests (ASan + UBSan) and the panel model tests
```

## Security notes

- Every way into the settings (hyprctl, Lua, `hyprripple.lua`, the panel)
  goes through the same whitelist validation (`native/src/Logic.cpp`,
  `Model.js`):
  - numbers are clamped
  - enums are checked
  - colors must be `#hex` or a theme key
  - `exclude` allows only `[A-Za-z0-9._-*?, ]`

  Nothing that reaches `hyprctl --batch` or the generated Lua can carry `;`,
  quotes or newlines.
- `hyprctl hyprripple get` output is JSON-escaped.
- `colors.toml` is only read if it's a regular file under 64 KiB, and only its
  first 512 lines.
- Class globs are matched without regex, in linear-ish time, so a hostile
  pattern can't stall the compositor.
- Held ripples give up after 8 s, and there are at most `max_ripples` (≤ 64).
- Manual triggers (hyprctl / Lua) are rate limited: a burst of 10, then 20 per
  second. Their radius is capped at 4000 px, so a looping script can't stack
  full-screen ripples and stall the compositor.
- `colors.toml` is size-checked on the open file descriptor (`fstat`), not the
  path, so it can't be swapped between the check and the read.
- Pause-while-sharing counts screenshare start/stop events per share.
  Hyprland's screenshare manager can't be queried safely from a plugin,
  because its singleton isn't exported and calling it could create a second
  one. If a stop event is ever missed: `hyprctl hyprripple sharing reset`.
- Inherent to Hyprland plugins and not fixable here: the plugin runs inside
  the compositor, and hyprpm builds whatever is committed in this folder, so
  anyone who can write here can get code into Hyprland on the next
  `hyprpm update`. Keep the folder owned by you and not group/world-writable.
  The version check is strict on purpose: it refuses to load against a
  different Hyprland build rather than risk a crash. The service sends a
  notification telling you to run `hyprpm update` when that happens.
- `hyprctl hyprripple` is reachable by any process that can talk to
  Hyprland's socket, the same as the rest of `hyprctl`.
