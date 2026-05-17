--- globals.lua
--- Shared configuration and runtime state for split-monitor-workspaces.
--
--- All other modules require() this file and mutate the tables directly.
--- Because require() caches its result, every module receives the same
--- table references, so mutations are visible everywhere.


---@class SMW.Config
---@field workspace_count integer?
---@field keep_focused boolean?
---@field enable_notifications boolean?
---@field enable_persistent_workspaces boolean?
---@field enable_wrapping boolean?
---@field link_monitors boolean?
---@field monitor_priority string[]?
---@field max_workspaces table<string, integer>?


---@class SMW.PriorityEntry
---@field value integer
---@field from_config boolean

local globals = {}

--- ============================================================
--- Configuration defaults
--- ============================================================

---@type SMW.Config
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
---@type table<integer, string[]>
globals.monitor_workspace_map = {}

--- monitor.name (string) -> { value = integer, from_config = boolean }
--- Tracks the priority of each monitor (lower value = higher priority = lower workspace IDs).
---@type table<string, SMW.PriorityEntry>
globals.monitor_priorities = {}

--- monitor.name (string) -> { value = integer, from_config = boolean }
--- Per-monitor workspace count overrides loaded from cfg.max_workspaces.
---@type table<string, SMW.PriorityEntry>
globals.monitor_max_ws_override = {}

return globals
