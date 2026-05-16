-- helpers.lua
-- Utility functions shared across the split-monitor-workspaces modules.

local globals = require("globals")
local helpers = {}

-- ============================================================
-- Notification helper
-- ============================================================

function helpers.notify(msg)
	if globals.cfg.enable_notifications then
		hl.notification.create({ text = "[split-monitor-workspaces] " .. msg, duration = 5000, icon = "info" })
	end
end

-- ============================================================
-- Direction parsing
-- ============================================================

-- Convert "next" / "prev" / "+N" / "-N" / "N" to a signed integer delta.
-- Returns 0 for unrecognised input.
function helpers.direction_to_delta(value)
	if value == "next" then return 1 end
	if value == "prev" then return -1 end
	local n = tonumber(value)
	print("[split-monitor-workspaces] Parsed direction value '" .. tostring(value) .. "' as delta " .. tostring(n))
	return n and math.floor(n) or 0
end

-- ============================================================
-- Monitor helpers
-- ============================================================

-- Return the effective workspace count for the named monitor,
-- respecting per-monitor overrides from the config.
function helpers.get_monitor_max_ws(monitor_name)
	local ov = globals.monitor_max_ws_override[monitor_name]
	return ov and ov.value or globals.cfg.workspace_count
end

-- Return the currently focused monitor, falling back to the monitor under the cursor.
function helpers.get_current_monitor()
	return hl.get_active_monitor() or hl.get_monitor_at_cursor()
end

-- Return the primary monitor to focus on startup/remap.
-- Respects cfg.default_monitor; falls back to the monitor with the lowest ID.
-- May return nil when no monitors are connected (e.g. during suspend/wake).
function helpers.get_primary_monitor()
	local monitors = hl.get_monitors()
	if #monitors == 0 then return nil end
	local default_monitor = hl.get_config("cursor.default_monitor")
	if default_monitor ~= "" then
		for _, m in ipairs(monitors) do
			if m.name == default_monitor then return m end
		end
	end

	local primary = monitors[1]
	for _, m in ipairs(monitors) do
		if m.id < primary.id then primary = m end
	end
	return primary
end

-- ============================================================
-- Priority / base-index calculation
--
-- The base index for a monitor is the sum of max_workspaces for
-- all monitors with a strictly lower priority value.
-- ============================================================

function helpers.calc_base_index(monitor_name)
	local prio = globals.monitor_priorities[monitor_name]
	if not prio then return 0 end

	local offset = 0
	for name, p in pairs(globals.monitor_priorities) do
		if p.value < prio.value then
			offset = offset + helpers.get_monitor_max_ws(name)
		end
	end
	return offset
end

-- ============================================================
-- Workspace name resolution
--
-- Given a monitor and a user-facing workspace string, returns the
-- absolute Hyprland workspace name assigned to that monitor.
--
-- Supported formats:
--   "empty"   -> first empty workspace on this monitor, or last if all occupied
--   "+N"/"-N" -> relative offset from the currently active workspace
--   "N"       -> 1-based index within the monitor's assigned range
--   other     -> treated as a named workspace and returned unchanged
-- ============================================================

function helpers.get_workspace_from_monitor(monitor, workspace_str)
	local ws_list = globals.monitor_workspace_map[monitor.id]
	if not ws_list or #ws_list == 0 then return workspace_str end

	if workspace_str == "empty" then
		for _, name in ipairs(ws_list) do
			local ws = hl.get_workspace(name)
			if ws == nil or ws.is_empty then return name end
		end
		return ws_list[#ws_list]
	end

	local first_char = workspace_str:sub(1, 1)
	local idx

	if first_char == "+" or first_char == "-" then
		-- relative offset from the current workspace
		local delta = helpers.direction_to_delta(workspace_str)
		if delta == 0 then return workspace_str end

		local active_name = monitor.active_workspace and monitor.active_workspace.name
		local cur = nil
		for i, name in ipairs(ws_list) do
			if name == active_name then
				cur = i; break
			end
		end
		if not cur then return workspace_str end
		idx = cur + delta
	else
		local n = tonumber(workspace_str)
		if not n then
			-- unrecognised -> treat as a named workspace passthrough
			return workspace_str
		end
		idx = math.floor(n) -- 1-based index into the monitor's workspace list
	end

	if idx < 1 then
		return globals.cfg.enable_wrapping and ws_list[#ws_list] or ws_list[1]
	elseif idx > #ws_list then
		return globals.cfg.enable_wrapping and ws_list[1] or ws_list[#ws_list]
	end
	return ws_list[idx]
end

return helpers
