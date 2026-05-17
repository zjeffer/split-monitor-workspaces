--- globals.lua
--- Shared configuration and runtime state for split-monitor-workspaces.
--
--- All other modules require() this file and mutate the tables directly.
--- Because require() caches its result, every module receives the same
--- table references, so mutations are visible everywhere.

local globals = {}

--- ============================================================
--- Configuration defaults
--- ============================================================

globals.cfg = {
	--- Number of workspaces to assign to each monitor.
	workspace_count = 10,

	--- If true, keep current workspaces focused on plugin init/reload.
	keep_focused = true,

	--- If true, show a Hyprland notification on remap and init.
	enable_notifications = false,

	--- If true, workspaces are kept alive even when empty.
	enable_persistent_workspaces = true,

	--- If true, cycling past the last/first workspace wraps around.
	enable_wrapping = true,

	--- If true, switching workspaces changes all monitors simultaneously (Gnome-style).
	link_monitors = false,

	--- List of monitor names in priority order (determines workspace range allocation).
	--- Monitors not listed get priorities assigned in the order Hyprland reports them.
	--- e.g. { "DP-1", "DP-2" }
	monitor_priority = {},

	--- Per-monitor workspace count overrides.
	--- e.g. { ["DP-1"] = 5, ["DP-2"] = 3 }
	max_workspaces = {},
}

--- ============================================================
--- Runtime state
--- ============================================================

--- monitor.id (integer) -> list of workspace name strings (e.g. {"11","12",...,"20"})
globals.monitor_workspace_map = {}

--- monitor.name (string) -> { value = integer, from_config = boolean }
--- Tracks the priority of each monitor (lower value = higher priority = lower workspace IDs).
globals.monitor_priorities = {}

--- monitor.name (string) -> { value = integer, from_config = boolean }
--- Per-monitor workspace count overrides loaded from cfg.max_workspaces.
globals.monitor_max_ws_override = {}

--- Event subscription handles.
--- Stored here to prevent Lua's garbage collector from destroying them.
globals.event_handles = {}

return globals
