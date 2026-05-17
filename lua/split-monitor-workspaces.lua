--- split-monitor-workspaces.lua
--- Entry point and public API for the split-monitor-workspaces Lua library.
--- See example.lua in the repo root for a usage example.


local globals     = require("globals")
local helpers     = require("helpers")
local monitors    = require("monitors")
local dispatchers = require("dispatchers")

local api         = {}

--- ==========================================================
--- Public API: dispatcher closures compatible with hl.bind()
--- ==========================================================

--- Switch to workspace N (1-indexed within the current monitor's range).
---@param workspace_str string The workspace to switch to, specified as a string. Also supports "+N", "-N", and "empty".
---@return fun(): nil
function api.workspace(workspace_str)
	return function() dispatchers.do_workspace(workspace_str) end
end

--- Cycle workspaces on the current monitor. Wrapping is controlled by globals.cfg.enable_wrapping.
---@param value string "next", "prev", "+N", "-N"
---@return fun(): nil
function api.cycle_workspaces(value)
	return function() dispatchers.do_cycle_workspaces(value, not globals.cfg.enable_wrapping) end
end

--- Move the active window to workspace N and follow it.
---@param workspace_str string The workspace to move to, specified as a string. Also supports "+N", "-N", and "empty".
---@return fun(): nil
function api.move_to_workspace(workspace_str)
	return function() dispatchers.do_move_to_workspace(workspace_str, false) end
end

--- Move the active window to workspace N silently (no focus change).
---@param workspace_str string The workspace to move to, specified as a string. Also supports "+N", "-N", and "empty".
---@return fun(): nil
function api.move_to_workspace_silent(workspace_str)
	return function() dispatchers.do_move_to_workspace(workspace_str, true) end
end

--- Move all windows not in any mapped workspace to the current workspace.
--- Useful for "grabbing" windows that would otherwise be "lost" on an unmapped monitor or after a config change.
---@return fun(): nil
function api.grab_rogue_windows()
	return function() dispatchers.do_grab_rogue_windows() end
end

--- ============================================================
--- Setup
--- ============================================================

---@param user_config SMW.Config?
function api.setup(user_config)
	--- Merge user config over defaults.
	if user_config then
		for k, v in pairs(user_config) do
			globals.cfg[k] = v
		end
	end

	--- Load monitor_priority list into the priorities map.
	for i, name in ipairs(globals.cfg.monitor_priority) do
		globals.monitor_priorities[name] = { value = i - 1, from_config = true }
	end

	--- Load per-monitor max_workspaces overrides.
	for name, count in pairs(globals.cfg.max_workspaces) do
		globals.monitor_max_ws_override[name] = { value = count, from_config = true }
	end

	--- Register event handlers.
	--- Handles are stored in globals.event_handles to prevent garbage collection.
	globals.event_handles.monitor_added   = hl.on("monitor.added", function(monitor)
		monitors.map_monitor(monitor)
	end)
	globals.event_handles.monitor_removed = hl.on("monitor.removed", function(monitor)
		monitors.unmap_monitor(monitor)
	end)
	globals.event_handles.config_reloaded = hl.on("config.reloaded", function()
		--- Clear auto-assigned priorities and overrides before remapping.
		for name, p in pairs(globals.monitor_priorities) do
			if not p.from_config then globals.monitor_priorities[name] = nil end
		end
		for name, p in pairs(globals.monitor_max_ws_override) do
			if not p.from_config then globals.monitor_max_ws_override[name] = nil end
		end
		monitors.remap_all_monitors()
	end)

	helpers.notify("Initialized successfully!")
	print("[split-monitor-workspaces] Setup complete. Workspaces will be mapped after config.reloaded.")
end

--- Get the configured number of workspaces each monitor should have.
--- Does not take into account per-monitor overrides from cfg.max_workspaces.
---@return integer
function api.get_amount_of_workspaces()
	return globals.cfg.workspace_count
end

return api
