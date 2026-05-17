--- Example Hyprland Lua config using split-monitor-workspaces.
--
--- Hyprland sets package.path to only search relative to the config directory,
--- so absolute-path require() calls do not work. Add the library directory to
--- package.path first, then require by module name.

package.path = package.path .. ";/path/to/split-monitor-workspaces/lua/?.lua"
local smw = require("split-monitor-workspaces")

smw.setup({
    --- Number of workspaces assigned to each monitor.
    workspace_count = 10,

    --- Monitor priority order: determines which monitor gets the lowest workspace IDs.
    --- Monitors not listed are assigned priorities in the order Hyprland reports them.
    monitor_priority = { "DP-1", "DP-2" },

    --- Per-monitor workspace count overrides (optional).
    --- Monitors not listed fall back to workspace_count.
    --- max_workspaces = { ["DP-1"] = 5, ["DP-2"] = 3 },

    --- Keep the currently focused workspace when the config is reloaded (recommended).
    keep_focused = true,

    --- Show a Hyprland notification on init and remap.
    --- enable_notifications = false,

    --- Keep workspaces alive even when empty.
    --- enable_persistent_workspaces = true,

    --- Wrap around when cycling past the first or last workspace.
    --- enable_wrapping = true,

    --- Switch all monitors simultaneously when changing workspaces (Gnome-style).
    --- link_monitors = false,

    --- Monitor to focus on startup/reload (same as cursor:default_monitor).
    --- default_monitor = "DP-1",
})

--- get_amount_of_workspaces is an easy helper function that simply returns the workspace_count you passed to the setup function.
for i = 1, smw.get_amount_of_workspaces() do
    local n = tostring(i)
    hl.bind("SUPER +" .. n, smw.workspace(n))                --- Switch to workspace N.
    hl.bind("SUPER +" .. n, smw.move_to_workspace_silent(n)) --- Move the active window to workspace N silently (no focus change).
end

--- Cycle workspaces on the current monitor.
hl.bind("SUPER + mouse_down", smw.cycle_workspaces("next"))
hl.bind("SUPER + mouse_up", smw.cycle_workspaces("prev"))

--- Move orphaned windows (not assigned to any mapped workspace) to the current workspace.
hl.bind("SUPER + G", smw.grab_rogue_windows())
