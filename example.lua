-- example.lua
-- Example Hyprland Lua config using split-monitor-workspaces.
--
-- Place this file (or its contents) in your Hyprland Lua config,
-- adjusting paths and keybinds to match your setup.
--
-- To load it from your main hyprland.lua:
--   require("/path/to/split-monitor-workspaces/example")

local smw = require("/path/to/split-monitor-workspaces/lua/split-monitor-workspaces")

smw.setup({
    -- Number of workspaces assigned to each monitor.
    workspace_count = 10,

    -- Monitor priority order — determines which monitor gets the lowest workspace IDs.
    -- Monitors not listed are assigned priorities in the order Hyprland reports them.
    monitor_priority = { "DP-1", "HDMI-1" },

    -- Per-monitor workspace count overrides (optional).
    -- max_workspaces = { ["DP-1"] = 5, ["HDMI-1"] = 3 },

    -- Keep the currently focused workspace when the config is reloaded.
    -- keep_focused = false,

    -- Show a Hyprland notification on init and remap.
    -- enable_notifications = false,

    -- Keep workspaces alive even when empty.
    -- enable_persistent_workspaces = true,

    -- Wrap around when cycling past the first or last workspace.
    -- enable_wrapping = true,

    -- Switch all monitors simultaneously when changing workspaces (Gnome-style).
    -- link_monitors = false,

    -- Use hy3 dispatchers for window-move operations.
    -- enable_hy3 = false,

    -- Monitor to focus on startup/reload (same as cursor:default_monitor).
    -- default_monitor = "DP-1",
})

for i = 1, smw.get_amount_of_workspaces() do
    local n = tostring(i)
    hl.bind("SUPER, " .. n, smw.workspace(n))                      -- Switch to workspace N.
    hl.bind("SUPER SHIFT, " .. n, smw.move_to_workspace(n))        -- Move the active window to workspace N and follow it.
    hl.bind("SUPER ALT, " .. n, smw.move_to_workspace_silent(n))   -- Move the active window to workspace N silently (no focus change).
end

-- Cycle workspaces on the current monitor.
hl.bind("SUPER, mouse_down", smw.cycle_workspaces("next"))
hl.bind("SUPER, mouse_up", smw.cycle_workspaces("prev"))

-- Move orphaned windows (not assigned to any mapped workspace) to the current workspace.
hl.bind("SUPER, G", smw.grab_rogue_windows())
