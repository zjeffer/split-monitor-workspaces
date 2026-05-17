--- monitors.lua
--- Monitor mapping: assigning workspace ranges to monitors and keeping
--- them in sync when monitors are added, removed, or config is reloaded.

local globals = require("globals")
local helpers = require("helpers")

local monitors = {}

--- ============================================================
--- Single-monitor mapping
--- ============================================================

function monitors.map_monitor(monitor)
	if monitor.is_mirror then
		print("[split-monitor-workspaces] Skipping mirrored monitor " .. monitor.name)
		return
	end

	--- Assign an auto-priority if this monitor has no configured priority.
	if not globals.monitor_priorities[monitor.name] then
		local max_prio = -1
		for _, p in pairs(globals.monitor_priorities) do
			if p.value > max_prio then max_prio = p.value end
		end
		globals.monitor_priorities[monitor.name] = { value = max_prio + 1, from_config = false }
	end

	local base    = helpers.calc_base_index(monitor.name)
	local max_ws  = helpers.get_monitor_max_ws(monitor.name)
	local start_i = base + 1
	local end_i   = base + max_ws

	print(string.format("[split-monitor-workspaces] Mapping workspaces %d-%d to monitor %s",
		start_i, end_i, monitor.name))

	--- Snapshot the monitor's active workspace *before* we touch anything.
	--- Used below to decide whether to switch focus when keep_focused is true:
	--- if the monitor is already showing a workspace in its assigned range we
	--- leave it alone; otherwise we initialise it to the first workspace.
	local prev_ws     = hl.get_active_workspace(monitor)
	local prev_ws_num = prev_ws and tonumber(prev_ws.name)
	local in_range    = prev_ws_num ~= nil
		and prev_ws_num >= start_i
		and prev_ws_num <= end_i

	globals.monitor_workspace_map[monitor.id] = {}
	if globals.cfg.enable_persistent_workspaces then
		for i = start_i, end_i do
			hl.workspace_rule({ workspace = tostring(i), persistent = true, monitor = monitor.name })
		end
	end
	for i = start_i, end_i do
		local ws_name = tostring(i)
		table.insert(globals.monitor_workspace_map[monitor.id], ws_name)

		local ws = hl.get_workspace(ws_name)
		if ws ~= nil then
			--- workspace already exists on some other monitor; move it here
			print("[split-monitor-workspaces] Moving existing workspace " .. ws_name .. " to monitor " .. monitor.name)
			hl.dispatch(hl.dsp.workspace.move({ workspace = ws_name, monitor = monitor.name }))
		elseif globals.cfg.enable_persistent_workspaces and i == start_i then
			--- eagerly create and assign the first workspace so the monitor has
			--- something to display; the rest are created on demand
			hl.dispatch(hl.dsp.focus({ workspace = ws_name }))
			hl.dispatch(hl.dsp.workspace.move({ workspace = ws_name, monitor = monitor.name }))
		end
	end

	--- Switch to the first workspace when keep_focused is off, or when the
	--- monitor is not already showing one of its assigned workspaces (which
	--- happens on first startup or after a monitor reconnect).
	local switched = not globals.cfg.keep_focused or not in_range
	if switched then
		hl.dispatch(hl.dsp.focus({ workspace = tostring(start_i) }))
	end
	return switched
end

--- ============================================================
--- Single-monitor unmapping
--- ============================================================

function monitors.unmap_monitor(monitor)
	--- Clear persistence rules for this monitor's workspaces so stale rules
	--- don't cause ensurePersistentWorkspacesPresent to move workspaces around.
	if globals.cfg.enable_persistent_workspaces then
		local ws_list = globals.monitor_workspace_map[monitor.id]
		if ws_list then
			for _, ws_name in ipairs(ws_list) do
				hl.workspace_rule({ workspace = ws_name, persistent = false })
			end
		end
	end

	--- remove auto-generated entries so they are recalculated on the next remap
	local prio = globals.monitor_priorities[monitor.name]
	if prio and not prio.from_config then
		globals.monitor_priorities[monitor.name] = nil
	end

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
	monitors.unmap_all_monitors()

	local any_switched = false
	for _, monitor in ipairs(hl.get_monitors()) do
		if monitors.map_monitor(monitor) then
			any_switched = true
		end
	end

	--- If any monitor had to switch workspace (i.e. keep_focused is off, or at
	--- least one monitor was being initialised for the first time), bring focus
	--- back to the primary monitor so the session always starts on the right
	--- screen.  When keep_focused is true and every monitor was already showing
	--- the correct workspace, no focus change is needed at all.
	if any_switched then
		local primary = helpers.get_primary_monitor()
		if primary then
			local ws_list = globals.monitor_workspace_map[primary.id]
			if ws_list and #ws_list > 0 then
				hl.dispatch(hl.dsp.focus({ workspace = ws_list[1] }))
			end
		end
	end
end

return monitors
