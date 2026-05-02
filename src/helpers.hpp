#pragma once

#include "globals.hpp"

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>

#include <cstdint>
#include <optional>
#include <string>

struct lua_State;

// Translate a raw plugin config key for the Lua config manager.
// The Lua manager stores keys with hyphens replaced by underscores; only '-'→'_'
// is applied here. The colon→dot translation is handled by the Lua manager itself.
const char* adaptConfigKey(const char* rawKey, Config::eConfigManagerType type);

void raiseNotification(const std::string& message, float timeout = 5000.0F);
bool isHy3Available();

// Call a Hyprland dispatcher directly through the keybind manager instead of
// going through invokeHyprctlCommand / IPC, which does not work correctly when
// called from within a Lua keybind callback context.
std::string runDispatcher(const std::string& dispatcher, const std::string& args);
std::string dispatchMoveToWorkspace(const std::string& workspaceName, bool silent);

int          getDelta(const std::string& direction);
int64_t      getMonitorMaxWorkspaces(const std::string& name);
PHLMONITOR   getPrimaryMonitor();
const std::string& getWorkspaceFromMonitor(const PHLMONITOR& monitor, const std::string& workspace);
PHLMONITOR   getCurrentMonitor();
int64_t      calcWorkspaceBaseIndex(const std::string& name);
std::string  configTypeToString(Config::eConfigManagerType type);

std::optional<std::string> luaArgToString(lua_State* L, int idx);
int pushLuaDispatchResult(lua_State* L, const SDispatchResult& result);
int pushLuaArgError(lua_State* L, const std::string& fn);
