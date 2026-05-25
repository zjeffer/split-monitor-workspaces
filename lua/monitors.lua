--- monitors.lua
--- Monitor mapping: assigning workspace ranges to monitors and keeping
--- them in sync when monitors are added, removed, or config is reloaded.

local current_path = (...):match("(.-)[^%.]+$") or ""
local globals      = require(current_path .. "globals")
local helpers      = require(current_path .. "helpers")

local monitors     = {}

--- ============================================================
--- Single-monitor mapping
--- ============================================================

---@param monitor HL.Monitor
---@return boolean|nil
function monitors.map_monitor(monitor)
	if monitor.is_mirror then
		print("[split-monitor-workspaces] Skipping mirrored monitor " .. monitor.name)
		return
	end

	--- Assign an auto-priority if this monitor has no configured priority.
	if not globals.monitor_priorities[monitor.name] then
		---@type integer
		local max_prio = -1
		for _, p in pairs(globals.monitor_priorities) do
			if p.value > max_prio then max_prio = p.value end
		end
		globals.monitor_priorities[monitor.name] = { value = max_prio + 1, from_config = false }
	end

	---@type integer
	local base    = helpers.calc_base_index(monitor.name)
	---@type integer
	local max_ws  = helpers.get_monitor_max_ws(monitor.name)
	local start_i = base + 1
	local end_i   = base + max_ws

	print(string.format("[split-monitor-workspaces] Mapping workspaces %d-%d to monitor %s",
		start_i, end_i, monitor.name))

	--- Snapshot the monitor's active workspace *before* we touch anything.
	--- Used below to decide whether to switch focus when keep_focused is true:
	--- if the monitor is already showing a workspace in its assigned range we
	--- leave it alone; otherwise we initialise it to the first workspace.
	---@type HL.Workspace|nil
	local prev_ws                             = hl.get_active_workspace(monitor)
	---@type number|nil
	local prev_ws_num                         = prev_ws and tonumber(prev_ws.name)
	---@type boolean
	local in_range                            = prev_ws_num ~= nil
		and prev_ws_num >= start_i
		and prev_ws_num <= end_i

	globals.monitor_workspace_map[monitor.id] = {}
	if globals.cfg.enable_persistent_workspaces then
		for i = start_i, end_i do
			hl.workspace_rule({ workspace = tostring(i), persistent = true, monitor = monitor.name })
		end
	end
	for i = start_i, end_i do
		---@type string
		local ws_name = tostring(i)
		table.insert(globals.monitor_workspace_map[monitor.id], ws_name)

		---@type HL.Workspace|nil
		local ws = hl.get_workspace(ws_name)
		if ws ~= nil then
			--- workspace already exists on some other monitor; move it here
			hl.dispatch(hl.dsp.workspace.move({ workspace = ws_name, monitor = monitor.name }))
		elseif i == start_i then
			--- workspace doesn't exist, let's create it and make sure it's on the correct monitor.
			--- Without this, only the last mapped monitor ends up with the correct workspace on startup (see issue #121).
			hl.dispatch(hl.dsp.focus({ workspace = ws_name }))
			hl.dispatch(hl.dsp.workspace.move({ workspace = ws_name, monitor = monitor.name }))
		end
	end

	--- Switch to the first workspace when keep_focused is off, or when the
	--- monitor is not already showing one of its assigned workspaces (which
	--- happens on first startup or after a monitor reconnect).
	---@type boolean
	local switched = not globals.cfg.keep_focused or not in_range
	if switched then
		hl.dispatch(hl.dsp.focus({ workspace = tostring(start_i) }))
	end
	return switched
end

--- ============================================================
--- Single-monitor unmapping
--- ============================================================

---@param monitor HL.Monitor
function monitors.unmap_monitor(monitor)
	--- Clear persistence rules for this monitor's workspaces so stale rules
	--- don't cause ensurePersistentWorkspacesPresent to move workspaces around.
	if globals.cfg.enable_persistent_workspaces then
		---@type string[]|nil
		local ws_list = globals.monitor_workspace_map[monitor.id]
		if ws_list then
			for _, ws_name in ipairs(ws_list) do
				hl.workspace_rule({ workspace = ws_name, persistent = false })
			end
		end
	end

	--- remove auto-generated entries so they are recalculated on the next remap
	---@type SMW.PriorityEntry|nil
	local prio = globals.monitor_priorities[monitor.name]
	if prio and not prio.from_config then
		globals.monitor_priorities[monitor.name] = nil
	end

	---@type SMW.PriorityEntry|nil
	local ov = globals.monitor_max_ws_override[monitor.name]
	if ov and not ov.from_config then
		globals.monitor_max_ws_override[monitor.name] = nil
	end

	globals.monitor_workspace_map[monitor.id] = nil
	print("[split-monitor-workspaces] Unmapped monitor " .. monitor.name)
end

--- ============================================================
--- Full unmap
--- ============================================================

function monitors.unmap_all_monitors()
	for _, monitor in ipairs(hl.get_monitors()) do
		monitors.unmap_monitor(monitor)
	end
	--- clear any stale entries for monitors no longer in the list
	globals.monitor_workspace_map = {}
end

--- ============================================================
--- Full remap (called on config.reloaded)
--- ============================================================

function monitors.remap_all_monitors()
	print("[split-monitor-workspaces] Remapping all monitors")

	--- When keep_focused is enabled, snapshot what every monitor is currently
	--- showing *before* any workspace changes happen. We restore the correct
	--- state in a second pass after all remapping is done.
	---@type HL.Monitor[]
	local monitor_list = hl.get_monitors()

	---@class SMW.SavedMonitorState
	---@field monitor HL.Monitor
	---@field ws_name string
	---@field is_focused boolean

	---@type table<integer, SMW.SavedMonitorState>
	local saved_monitors = {}
	---@type integer|nil
	local focused_id = nil
	if globals.cfg.keep_focused then
		---@type HL.Monitor|nil
		local active = hl.get_active_monitor()
		if active then focused_id = active.id end
		for _, m in ipairs(monitor_list) do
			---@type HL.Workspace|nil
			local ws = hl.get_active_workspace(m)
			if ws then
				saved_monitors[m.id] = {
					monitor    = m,
					ws_name    = ws.name,
					is_focused = (m.id == focused_id),
				}
			end
		end
	end

	monitors.unmap_all_monitors()

	---@type boolean
	local any_switched = false
	for _, monitor in ipairs(monitor_list) do
		if monitors.map_monitor(monitor) then
			any_switched = true
		end
	end

	if globals.cfg.keep_focused and next(saved_monitors) ~= nil then
		--- Restore each monitor's visible workspace.  Non-focused monitors are
		--- restored first so that the final focus() call lands on the monitor
		--- that was originally focused, leaving the session exactly as the user
		--- had it before the reload.  Monitors whose saved workspace falls
		--- outside the new assigned range are skipped (map_monitor already
		--- focused their start workspace, which is the correct fallback).
		---@type SMW.SavedMonitorState|nil
		local restore_focused = nil
		for _, state in pairs(saved_monitors) do
			---@type number|nil
			local ws_num = tonumber(state.ws_name)
			---@type integer
			local base   = helpers.calc_base_index(state.monitor.name)
			---@type integer
			local max_ws = helpers.get_monitor_max_ws(state.monitor.name)
			local si     = base + 1
			local ei     = base + max_ws
			if ws_num and ws_num >= si and ws_num <= ei then
				if state.is_focused then
					restore_focused = state
				else
					--- Only dispatch if the workspace isn't already visible on this monitor.
					--- Skipping avoids a cursor warp to the non-focused monitor.
					---@type HL.Workspace|nil
					local cur = hl.get_active_workspace(state.monitor)
					if not cur or cur.name ~= state.ws_name then
						hl.dispatch(hl.dsp.focus({ workspace = state.ws_name }))
					end
				end
			end
		end
		if restore_focused then
			--- Only dispatch if the workspace isn't already focused.
			--- Skipping avoids a cursor warp to the monitor centre on reload.
			---@type HL.Workspace|nil
			local cur = hl.get_active_workspace(restore_focused.monitor)
			if not cur or cur.name ~= restore_focused.ws_name then
				hl.dispatch(hl.dsp.focus({ workspace = restore_focused.ws_name }))
			end
		end
	elseif any_switched then
		--- keep_focused is off (or no snapshot was taken): bring focus back to
		--- the primary monitor so the session always starts on the right screen.
		---@type HL.Monitor|nil
		local primary = helpers.get_primary_monitor()
		if primary then
			---@type string[]|nil
			local ws_list = globals.monitor_workspace_map[primary.id]
			if ws_list and #ws_list > 0 then
				hl.dispatch(hl.dsp.focus({ workspace = ws_list[1] }))
			end
		end
	end
end

return monitors
