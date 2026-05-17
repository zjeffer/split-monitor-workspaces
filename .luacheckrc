-- .luacheckrc - luacheck configuration for split-monitor-workspaces
--
-- `hl` is the Hyprland Lua API global, injected at runtime by Hyprland.
-- It is not defined in any of our source files, so we declare it here
-- to suppress false-positive "undefined variable" warnings.
read_globals = { "hl" }

-- default is 120
max_line_length = 160
