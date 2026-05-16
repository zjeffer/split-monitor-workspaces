-- dispatchers.lua
-- Imperative implementations of each split-monitor-workspaces action.
-- These are called at runtime (from keybind callbacks and event handlers).
--
-- Each function in this module has a corresponding public closure factory
-- in split-monitor-workspaces.lua that wraps it for use with hl.bind().

local globals = require("globals")
local helpers = require("helpers")

local dispatchers = {}

-- ============================================================
-- split-workspace
-- ============================================================

function dispatchers.do_workspace(workspace_str)
	if globals.cfg.link_monitors then
		-- Gnome-style: switch all monitors to their corresponding workspace.
		local current = helpers.get_current_monitor()
		for _, monitor in ipairs(hl.get_monitors()) do
			local resolved = helpers.get_workspace_from_monitor(monitor, workspace_str)
			-- create the workspace if it does not yet exist
			if hl.get_workspace(resolved) == nil then
				hl.dispatch(hl.dsp.focus({ workspace = resolved }))
			end
			hl.dispatch(hl.dsp.workspace.move({ workspace = resolved, monitor = monitor.name }))
			if current and monitor.id == current.id then
				hl.dispatch(hl.dsp.focus({ workspace = resolved }))
			end
		end
	else
		local monitor = helpers.get_current_monitor()
		if not monitor then return end
		local resolved = helpers.get_workspace_from_monitor(monitor, workspace_str)
		hl.dispatch(hl.dsp.focus({ workspace = resolved }))
	end
end

-- ============================================================
-- split-cycleworkspaces
-- ============================================================

function dispatchers.do_cycle_workspaces(value, no_wrap)
	local delta = helpers.direction_to_delta(value)
	if delta == 0 then
		print("[split-monitor-workspaces] Invalid cycle value: " .. tostring(value))
		return
	end

	local monitors_to_cycle
	if globals.cfg.link_monitors then
		monitors_to_cycle = hl.get_monitors()
	else
		local m = helpers.get_current_monitor()
		monitors_to_cycle = m and { m } or {}
	end

	for _, monitor in ipairs(monitors_to_cycle) do
		local ws_list = globals.monitor_workspace_map[monitor.id]
		if not ws_list then goto continue end

		local active_name = monitor.active_workspace and monitor.active_workspace.name
		local idx = nil
		for i, name in ipairs(ws_list) do
			if name == active_name then
				idx = i; break
			end
		end
		if not idx then goto continue end

		idx = idx + delta
		if idx < 1 then
			if no_wrap then goto continue end
			idx = #ws_list
		elseif idx > #ws_list then
			if no_wrap then goto continue end
			idx = 1
		end

		local target = ws_list[idx]
		if globals.cfg.link_monitors then
			hl.dispatch(hl.dsp.workspace.move({ workspace = target, monitor = monitor.name }))
			if monitor.focused then
				hl.dispatch(hl.dsp.focus({ workspace = target }))
			end
		else
			hl.dispatch(hl.dsp.focus({ workspace = target }))
		end

		::continue::
	end
end

-- ============================================================
-- split-movetoworkspace / split-movetoworkspacesilent
-- ============================================================

function dispatchers.do_move_to_workspace(workspace_str, silent)
	local monitor = helpers.get_current_monitor()
	if not monitor then return end
	local resolved = helpers.get_workspace_from_monitor(monitor, workspace_str)

	if hl.plugin.hy3 then
		if silent then
			hl.dispatch(hl.plugin.hy3.move_to_workspace(resolved))
		else
			hl.dispatch(hl.plugin.hy3.move_to_workspace(resolved, { follow = true }))
		end
	else
		if silent then
			hl.dispatch(hl.dsp.window.move({ workspace = resolved, follow = false }))
		else
			hl.dispatch(hl.dsp.window.move({ workspace = resolved }))
		end
	end

	if globals.cfg.link_monitors and not silent then
		dispatchers.do_workspace(workspace_str)
	end
end

-- ============================================================
-- split-grabroguewindows
-- ============================================================

function dispatchers.do_grab_rogue_windows()
	local current_monitor = helpers.get_current_monitor()
	if not current_monitor then return end
	local current_ws = current_monitor.active_workspace
	if not current_ws then return end

	-- build a set of all workspace names that belong to any mapped monitor
	local mapped = {}
	for _, ws_list in pairs(globals.monitor_workspace_map) do
		for _, name in ipairs(ws_list) do
			mapped[name] = true
		end
	end

	local windows = hl.get_windows()
	for _, window in ipairs(windows) do
		if not window.mapped then goto continue end
		if not window.workspace then goto continue end
		if window.workspace.special then goto continue end

		if not mapped[window.workspace.name] then
			print(string.format(
				"[split-monitor-workspaces] Moving rogue window '%s' from workspace %s to %s",
				window.title, window.workspace.name, current_ws.name))
			-- use the address selector to target a specific non-active window
			hl.dispatch(hl.dsp.window.move({ workspace = current_ws.name, window = window, follow = false }))
		end

		::continue::
	end
end

return dispatchers
