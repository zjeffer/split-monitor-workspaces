-- monitors.lua
-- Monitor mapping: assigning workspace ranges to monitors and keeping
-- them in sync when monitors are added, removed, or config is reloaded.

local globals = require("globals")
local helpers = require("helpers")

local monitors = {}

-- ============================================================
-- Workspace persistence rules
-- Must be called at config-load time (inside setup()), before any
-- workspaces are created, so the rules are in effect when workspaces
-- come into existence later via monitor.added events.
-- ============================================================

function monitors.setup_persistence_rules()
	if not globals.cfg.enable_persistent_workspaces then return end
	local total = globals.cfg.max_monitors * globals.cfg.workspace_count
	for i = 1, total do
		hl.workspace_rule({ workspace = tostring(i), persistent = true })
	end
end

-- ============================================================
-- Single-monitor mapping
-- ============================================================

function monitors.map_monitor(monitor)
	if monitor.is_mirror then
		print("[split-monitor-workspaces] Skipping mirrored monitor " .. monitor.name)
		return
	end

	-- Assign an auto-priority if this monitor has no configured priority.
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

	globals.monitor_workspace_map[monitor.id] = {}
	for i = start_i, end_i do
		local ws_name = tostring(i)
		table.insert(globals.monitor_workspace_map[monitor.id], ws_name)

		local ws = hl.get_workspace(ws_name)
		if ws ~= nil then
			-- workspace already exists on some other monitor; move it here
			hl.dispatch(hl.dsp.workspace.move({ workspace = ws_name, monitor = monitor.name }))
		elseif globals.cfg.enable_persistent_workspaces and i == start_i then
			-- eagerly create and assign the first workspace so the monitor has
			-- something to display; the rest are created on demand
			hl.dispatch(hl.dsp.focus({ workspace = ws_name }))
			hl.dispatch(hl.dsp.workspace.move({ workspace = ws_name, monitor = monitor.name }))
		end
	end

	if not globals.cfg.keep_focused or globals.first_load then
		hl.dispatch(hl.dsp.focus({ workspace = tostring(start_i) }))
	end
end

-- ============================================================
-- Single-monitor unmapping
-- ============================================================

function monitors.unmap_monitor(monitor)
	-- remove auto-generated entries so they are recalculated on the next remap
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

-- ============================================================
-- Full unmap
-- ============================================================

function monitors.unmap_all_monitors()
	for _, monitor in ipairs(hl.get_monitors()) do
		monitors.unmap_monitor(monitor)
	end
	-- clear any stale entries for monitors no longer in the list
	globals.monitor_workspace_map = {}
end

-- ============================================================
-- Full remap (called on config.reloaded)
-- ============================================================

function monitors.remap_all_monitors()
	helpers.notify("Remapping workspaces...")
	print("[split-monitor-workspaces] Remapping all monitors")
	monitors.unmap_all_monitors()

	for _, monitor in ipairs(hl.get_monitors()) do
		monitors.map_monitor(monitor)
	end

	if not globals.cfg.keep_focused or globals.first_load then
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
