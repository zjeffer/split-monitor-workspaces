local current_path = ((...):match("^(.*)%.init$") or (...)) .. "."
return require(current_path .. "lua.split-monitor-workspaces")
