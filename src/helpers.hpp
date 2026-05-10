#pragma once

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>

#include <cstdint>
#include <optional>
#include <string>

struct lua_State;

// Translate a raw plugin config key for the Lua config manager. For Lua, we need to replace dashes with underscores.
const char* translateConfigKey(const char* rawKey);

// Get a config value of the specified type.
template <typename T> std::optional<T> getConfigValue(const char* paramName);

// Raise a Hyprland notification. Nullop if notifications are disabled in the plugin config.
void raiseNotification(const std::string& message, float timeout = 5000.0F);

// hy3 detection
bool isHy3Available();

// Call a Hyprland dispatcher directly through the keybind manager.
std::string runDispatcher(const std::string& dispatcher, const std::string& args);
std::string dispatchMoveToWorkspace(const std::string& workspaceName, bool silent);

// From a given direction (prev/next/+x/-x), calculate the delta to apply to the current workspace index. Returns 0 if the input is invalid.
int directionToDelta(const std::string& direction);

// Get the maximum number of workspaces for a given monitor, either from the config or the default value.
int64_t getMonitorMaxWorkspaces(const std::string& name);

// Determien the primary monitor to use on startup, based on the plugin config and available monitors.
// Can return nullptr if no monitors are found (can happen when reloading during suspend/wake)
PHLMONITOR getPrimaryMonitor();

// From a workspace string and monitor, determine the actual workspace name Hyprland has given it.
// The given workspace string can be in multiple formats: "empty", relative ("+1", "-2"), absolute ("1", "2") or a workspace name.
const std::string& getWorkspaceFromMonitor(const PHLMONITOR& monitor, const std::string& workspace);

// Get the currently focused monitor, with a fallback to the monitor under the cursor if the focused monitor can't be determined
PHLMONITOR getCurrentMonitor();

// Calculate the base workspace index for a given monitor based on its priority.
int64_t calcWorkspaceBaseIndex(const std::string& name);

// Convert Hyprland's eConfigManagerType to a human-readable string for logging.
std::string configTypeToString(Config::eConfigManagerType type);

/////// Helpers for Lua bindings

// Coerce the top Lua argument to a string, converting integers to strings as well. Returns nullopt if the argument is not a string or integer.
std::optional<std::string> luaArgToString(lua_State* L, int idx);

// Push a result table for Lua dispatch functions, with an "ok" boolean and an "error" string.
int pushLuaDispatchResult(lua_State* L, const SDispatchResult& result);

// Push a Lua error result for invalid arguments.
int pushLuaArgError(lua_State* L, const std::string& fn);

/////// Template implementations
#include "helpers.txx"
