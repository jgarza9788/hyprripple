-- HyprRipple: Material Design ink ripples (jgarza.hyprripple).
-- Written by the HyprRipple panel: omarchy-shell shell toggle jgarza.hyprripple
-- Hand edits are fine - the panel reads this file back. Keep one key per line.
-- Live, without a reload: hyprctl hyprripple set <key> <value>

if hl.plugin.hyprripple then
  hl.config({ plugin = { hyprripple = {
    on_click = "left",
    on_focus = true,
    focus_hover = false,
    on_workspace = true,
    color = "accent",
    style = "classic",
    opacity = 0.28,
    radius = 140,
    softness = 2,
    ring_width = 6,
    duration = 550,
    focus_origin = "cursor",
    bounded = true,
    workspace_radius = "screen",
    max_ripples = 16,
    enabled = true,
  } } })
end
