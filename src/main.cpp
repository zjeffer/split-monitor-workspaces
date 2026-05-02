#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/string/VarList2.hpp>

#include "globals.hpp"

extern "C" {
#include <lua.h>
}

#include <map>
#include <optional>
#include <type_traits>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

auto constexpr k_workspaceCount = "plugin:split-monitor-workspaces:count";
auto constexpr k_keepFocused = "plugin:split-monitor-workspaces:keep_focused";
auto constexpr k_enableNotifications = "plugin:split-monitor-workspaces:enable_notifications";
auto constexpr k_enablePersistentWorkspaces = "plugin:split-monitor-workspaces:enable_persistent_workspaces";
auto constexpr k_enableWrapping = "plugin:split-monitor-workspaces:enable_wrapping";
auto constexpr k_defaultMonitor = "cursor:default_monitor";
auto constexpr k_monitorPriority = "plugin:split-monitor-workspaces:monitor_priority";
auto constexpr k_monitorMaxWorkspaces = "plugin:split-monitor-workspaces:max_workspaces";
auto constexpr k_linkMonitors = "plugin:split-monitor-workspaces:link_monitors";
auto constexpr k_enableHy3 = "plugin:split-monitor-workspaces:enable_hy3";
auto constexpr k_luaNamespace = "split_monitor_workspaces";

static const CHyprColor s_pluginColor = {0x61 / 255.0F, 0xAF / 255.0F, 0xEF / 255.0F, 1.0F};

static int g_workspaceCount;
static bool g_keepFocused = false;
static bool g_enableNotifications = false;
static bool g_enablePersistentWorkspaces = true;
static bool g_enableWrapping = true;
static std::string g_defaultMonitor = "";
static bool g_linkMonitors = false;

// Hy3 explicit support
enum class Hy3Status {
    DISABLED,          // disabled in config
    DETECTION_PENDING, // not yet performed
    NOT_DETECTED,      // detection failed
    DETECTED,          // detection succeeded
};
static Hy3Status g_hy3Status = Hy3Status::DETECTION_PENDING;

// the first time we load the plugin, we want to switch to the first workspace on the primary monitor regardless of keepFocused
static bool g_firstLoad = true;

static std::map<MONITORID, std::vector<std::string>> g_vMonitorWorkspaceMap;
static std::vector<PHLWORKSPACE> g_vPersistentWorkspaces; // to keep ownership of persistent workspaces, otherwise Hyprland will remove them

struct MonitorConfigValue {
    int64_t value = 0;
    bool wasSetFromConfig = false;

    // favor value in usage
    operator int64_t() const
    {
        return value;
    }
    int64_t operator=(int64_t v)
    {
        value = v;
        return value;
    }
};

static std::map<std::string, MonitorConfigValue> g_vMonitorPriorities;
static std::map<std::string, MonitorConfigValue> g_vMonitorMaxWorkspaces;

static CHyprSignalListener e_monitorAddedHandle = nullptr;
static CHyprSignalListener e_monitorRemovedHandle = nullptr;
static CHyprSignalListener e_configReloadedHandle = nullptr;
static CHyprSignalListener e_preConfigReloadHandle = nullptr;

static const char* configName(const char* name)
{
    if (Config::mgr()->type() != Config::CONFIG_LUA || !std::string_view{name}.starts_with("plugin:"))
        return name;

    static std::map<std::string, std::string> luaNames;

    auto [it, inserted] = luaNames.try_emplace(name);
    if (inserted) {
        it->second = it->first;
        std::ranges::replace(it->second, '-', '_');
    }

    return it->second.c_str();
}

static void raiseNotification(const std::string& message, float timeout = 5000.0F)
{
    if (g_enableNotifications) {
        HyprlandAPI::addNotification(PHANDLE, message, s_pluginColor, timeout);
    }
}

static std::optional<std::string> luaArgToString(lua_State* L, int idx)
{
    if (lua_isinteger(L, idx))
        return std::to_string(lua_tointeger(L, idx));

    const auto* str = lua_tostring(L, idx);
    if (str == nullptr)
        return std::nullopt;

    return std::string{str};
}

static int pushLuaDispatchResult(lua_State* L, const SDispatchResult& result)
{
    lua_newtable(L);

    lua_pushboolean(L, result.success);
    lua_setfield(L, -2, "ok");

    lua_pushboolean(L, result.passEvent);
    lua_setfield(L, -2, "pass_event");

    if (!result.success) {
        lua_pushstring(L, result.error.c_str());
        lua_setfield(L, -2, "error");
    }

    return 1;
}

static int pushLuaArgError(lua_State* L, const std::string& fn)
{
    return pushLuaDispatchResult(L, {.success = false, .error = fn + " expects a string or integer argument"});
}

static bool isHy3Available()
{
    if (g_hy3Status == Hy3Status::DISABLED || g_hy3Status == Hy3Status::NOT_DETECTED)
        return false;

    if (g_hy3Status == Hy3Status::DETECTED)
        return true;

    // detection: check if the hy3 plugin registered its dispatcher
    if (g_pKeybindManager->m_dispatchers.contains("hy3:movetoworkspace")) {
        g_hy3Status = Hy3Status::DETECTED;
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] hy3 plugin detected, using hy3 dispatchers for move operations");
        return true;
    }

    g_hy3Status = Hy3Status::NOT_DETECTED;
    return false;
}

static SDispatchResult runHyprlandDispatcher(const std::string& dispatcher, const std::string& args)
{
    if (!g_pKeybindManager)
        return {.success = false, .error = "Keybind manager is not available"};

    const auto it = g_pKeybindManager->m_dispatchers.find(dispatcher);
    if (it == g_pKeybindManager->m_dispatchers.end())
        return {.success = false, .error = "Dispatcher not found: " + dispatcher};

    return it->second(args);
}

static std::string runHyprlandDispatcherString(const std::string& dispatcher, const std::string& args)
{
    const auto result = runHyprlandDispatcher(dispatcher, args);
    return result.success ? "ok" : result.error;
}

static std::string dispatchMoveToWorkspace(const std::string& workspaceName, bool silent)
{
    if (isHy3Available()) {
        if (silent) {
            return runHyprlandDispatcherString("hy3:movetoworkspace", workspaceName);
        }
        return runHyprlandDispatcherString("hy3:movetoworkspace", workspaceName + ",follow");
    }
    if (silent) {
        return runHyprlandDispatcherString("movetoworkspacesilent", workspaceName);
    }
    return runHyprlandDispatcherString("movetoworkspace", workspaceName);
}

// avoid default initialization with []
int64_t getMonitorMaxWorkspaces(const std::string& name)
{
    return g_vMonitorMaxWorkspaces.contains(name) ? g_vMonitorMaxWorkspaces[name] : g_workspaceCount;
}

static int getDelta(const std::string& direction)
{
    if (direction == "next")
        return 1;
    if (direction == "prev")
        return -1;
    try {
        // this supports -x, +x and x
        return std::stoi(direction);
    }
    catch (const std::invalid_argument&) {
        Log::logger->log(Log::DEBUG, "[split-monitor-workspaces] Invalid direction value: {}", direction.c_str());
    }
    // fallback if input is incorrect
    return 0;
}

template <typename T> static auto getConfigValue(const char* paramName)
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Getting config value {}", paramName);

    const auto reply = Config::mgr()->getConfigValue(paramName);
    if (reply.dataptr == nullptr || *reply.dataptr == nullptr) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Failed to get config value {}", paramName);
        if constexpr (std::is_same_v<T, Config::STRING>)
            return std::string{};
        else
            return T{0};
    }

    const auto* const paramPtr = reinterpret_cast<const T*>(*reply.dataptr);
    if (paramPtr == nullptr) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Config value {} was null", paramName);
        if constexpr (std::is_same_v<T, Config::STRING>)
            return std::string{};
        else
            return T{0};
    }

    if constexpr (std::is_same_v<T, Config::STRING>) {
        auto paramStr = std::string{*paramPtr};
        // strip leading and trailing quotes if any (god I hate toml)
        if (paramStr.size() >= 2 && paramStr.front() == '"' && paramStr.back() == '"') {
            paramStr = paramStr.substr(1, paramStr.size() - 2);
        }
        return paramStr;
    }

    return *paramPtr;
}

static void loadMonitorPriority(const std::string& raw)
{
    if (raw.empty())
        return;

    const auto args = Hyprutils::String::CVarList2(raw.c_str());

    int64_t priorityCounter = 0;
    for (const auto& arg : args) {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Setting monitor priority: {} -> {}", arg, priorityCounter);
        g_vMonitorPriorities[std::string(arg)] = {.value = priorityCounter, .wasSetFromConfig = true};
        priorityCounter++;
    }
}

static void loadMonitorMaxWorkspaces(const std::string& raw)
{
    if (raw.empty())
        return;

    size_t start = 0;
    while (start <= raw.size()) {
        const auto end = raw.find(';', start);
        const auto entry = raw.substr(start, end == std::string::npos ? std::string::npos : end - start);

        if (!entry.empty()) {
            const auto args = Hyprutils::String::CVarList2(entry.c_str());
            if (args.size() != 2) {
                Log::logger->log(Log::ERR,
                                 "[split-monitor-workspaces] Invalid max_workspaces entry '{}', expected 'MONITOR, COUNT' entries separated by ';'",
                                 entry.c_str());
            }
            else {
                try {
                    auto monitorName = std::string(args[0]);
                    const auto maxWorkspaces = std::stoi(std::string(args[1]));

                    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Setting monitor max workspaces from Lua config: {} -> {}", monitorName.c_str(),
                                     maxWorkspaces);
                    g_vMonitorMaxWorkspaces[monitorName] = {.value = maxWorkspaces, .wasSetFromConfig = true};
                }
                catch (const std::exception& e) {
                    Log::logger->log(Log::ERR, "[split-monitor-workspaces] Failed to parse max_workspaces entry '{}': {}", entry.c_str(), e.what());
                }
            }
        }

        if (end == std::string::npos)
            break;

        start = end + 1;
    }
}

static PHLMONITOR getPrimaryMonitor()
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Determining primary monitor");
    // The hyprland config can specify a default monitor to focus on startup, the plugin respects that setting
    if (!g_defaultMonitor.empty()) {
        for (const PHLMONITOR& monitor : g_pCompositor->m_monitors) {
            if (monitor->m_name == g_defaultMonitor) {
                Log::logger->log(Log::INFO, "[split-monitor-workspaces] Using default monitor '{}' from config", g_defaultMonitor.c_str());
                return monitor;
            }
        }
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Default monitor '{}' not found, will use monitor with lowest ID as ", g_defaultMonitor.c_str());
    }
    // default monitor not set, let's use the monitor with the lowest ID
    // but let's first filter out invalid monitors (likely will never happen I assume, but just in case)
    auto validMonitors = g_pCompositor->m_monitors | std::views::filter([](const PHLMONITOR& m) { return m->m_id != MONITOR_INVALID; });
    auto const primaryMonitorIt = std::ranges::min_element(validMonitors, std::ranges::less{}, [](const PHLMONITOR& m) { return m->m_id; });
    if (primaryMonitorIt != validMonitors.end()) {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Using monitor '{}' with lowest ID {} as primary monitor", (*primaryMonitorIt)->m_name.c_str(), (*primaryMonitorIt)->m_id);
        return *primaryMonitorIt;
    }
    Log::logger->log(Log::ERR, "[split-monitor-workspaces] No valid monitors found?");
    throw std::runtime_error("split-monitor-workspaces: No valid monitors found?");
}

static const std::string& getWorkspaceFromMonitor(const PHLMONITOR& monitor, const std::string& workspace)
{
    // based on the string, we parse multiple formats:
    // #1 - "empty" -> get the first empty workspace on the monitor, or the last workspace if all have windows
    // #2 - "+1", "-2" -> relative workspace ID, e.g. next or previous workspace
    // #3 - "1", "2", "3" -> absolute workspace ID, e.g. workspace 1, 2 or 3 on the current monitor
    // if these formats fail to be parsed from the workspace string, we assume the user wants to switch to a workspace by name and simply pass that to hyprland

    auto const curWorkspacesIt = g_vMonitorWorkspaceMap.find(monitor->m_id);
    if (curWorkspacesIt == g_vMonitorWorkspaceMap.end()) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Monitor ID {} not found in workspace map", monitor->m_id);
        return workspace; // use the original string if the monitor is not mapped
    }
    const std::vector<std::string>& curWorkspaces = curWorkspacesIt->second;
    if (curWorkspaces.empty()) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] No workspaces mapped to monitor ID {}", monitor->m_id);
        return workspace; // use the original string if no workspaces are mapped
    }

    // #1 if the workspace is "empty", we expect the new ID to be the first available ID on the given monitor (not the first ID in the global list)
    if (workspace == "empty") {
        // get the next workspace ID that is empty on this monitor
        for (const auto& workspaceName : curWorkspaces) {
            PHLWORKSPACE workspacePtr = g_pCompositor->getWorkspaceByName(workspaceName);
            // the workspace we want is either not yet created (=nullptr) or already created but empty (!= nullptr but no windows)
            if (workspacePtr == nullptr || workspacePtr->getWindows() == 0) {
                return workspaceName;
            }
        }
        // if no empty monitor, we just go to the last workspace in the map
        return curWorkspaces.back();
    }

    int workspaceIndex = 0;
    if (workspace.starts_with("+") || workspace.starts_with("-")) {
        // #2 relative IDS, e.g. +1, -2
        auto delta = getDelta(workspace);
        if (delta == 0) {
            Log::logger->log(Log::ERR, "[split-monitor-workspaces] Invalid workspace delta: {}", workspace.c_str());
            return workspace;
        }
        // find the current workspace index in the monitor's workspace list
        auto it = std::ranges::find(curWorkspaces, monitor->m_activeWorkspace->m_name);
        if (it == curWorkspaces.end()) {
            Log::logger->log(Log::ERR, "[split-monitor-workspaces] Current workspace {} not found in monitor workspaces", monitor->m_activeWorkspace->m_name.c_str());
            return workspace;
        }
        workspaceIndex = std::distance(curWorkspaces.begin(), it) + delta;
    }
    else {
        // #3 absolute IDs, e.g. 1, 2, 3
        try {
            // convert to 0-indexed int
            workspaceIndex = std::stoi(workspace) - 1;
        }
        catch (std::invalid_argument&) {
            // if parsing fails, assume the user wants to switch to the workspace by name
            Log::logger->log(Log::WARN, "[split-monitor-workspaces] Invalid workspace index: {}, assuming named workspace", workspace.c_str());
            return workspace;
        }
    }

    if (workspaceIndex < 0) {
        if (g_enableWrapping) {
            return curWorkspaces.back(); // wrap around to the last workspace
        }
        return curWorkspaces.front(); // stop at the first workspace
    }

    if ((size_t)workspaceIndex >= curWorkspaces.size()) {
        if (g_enableWrapping) {
            return curWorkspaces.front(); // wrap around to the first workspace
        }
        return curWorkspaces.back(); // stop at the last workspace
    }

    return curWorkspaces[workspaceIndex];
}

static PHLMONITOR getCurrentMonitor()
{
    // get last focused monitor, because some people switch monitors with a keybind while the cursor is on a different monitor
    if (PHLMONITOR monitor = Desktop::focusState()->monitor()) {
        return monitor;
    }
    Log::logger->log(Log::WARN, "[split-monitor-workspaces] Last monitor does not exist, falling back to cursor's monitor");
    // fallback to the monitor the cursor is on
    return g_pCompositor->getMonitorFromCursor();
}

static SDispatchResult splitWorkspace(const std::string& workspace)
{
    if (!g_linkMonitors) {
        // not linked => just change workspace on current monitor
        return runHyprlandDispatcher("workspace", getWorkspaceFromMonitor(getCurrentMonitor(), workspace));
    }
    // workspaces are linked => change workspace on all monitors
    std::vector<SDispatchResult> results;
    for (const PHLMONITOR& monitor : g_pCompositor->m_monitors) {
        bool noFocus = monitor != getCurrentMonitor(); // only focus the current monitor
        auto workspaceRef = g_pCompositor->getWorkspaceByName(getWorkspaceFromMonitor(monitor, workspace));
        if (workspaceRef.get() == nullptr) {
            // create it if it doesn't exist yet
            auto const workspaceID = getWorkspaceIDNameFromString(getWorkspaceFromMonitor(monitor, workspace)).id;
            workspaceRef = g_pCompositor->createNewWorkspace(workspaceID, monitor->m_id);
        }
        monitor->changeWorkspace(workspaceRef, false, true, noFocus);
    }
    return {.success = true, .error = ""};
}

static SDispatchResult cycleWorkspaces(const std::string& value, bool nowrap = false)
{
    int const delta = getDelta(value);
    if (delta == 0) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Invalid cycle value: {}", value.c_str());
        return {.success = false, .error = "Invalid cycle value: " + value};
    }

    auto const monitorsToCycle = g_linkMonitors ? g_pCompositor->m_monitors : std::vector<PHLMONITOR>{getCurrentMonitor()};

    for (const PHLMONITOR& monitor : monitorsToCycle) {
        Log::logger->log(Log::DEBUG, "[split-monitor-workspaces] Cycling workspace on monitor {} (ID {}) by {}", monitor->m_name, monitor->m_id, delta);
        auto const workspaces = g_vMonitorWorkspaceMap[monitor->m_id];
        int index = -1;
        for (int i = 0; i < getMonitorMaxWorkspaces(monitor->m_name); i++) {
            if (workspaces[i] == monitor->m_activeWorkspace->m_name) {
                index = i;
                break;
            }
        }
        if (index == -1) {
            Log::logger->log(Log::WARN, "[split-monitor-workspaces] Could not find active workspace in monitor workspaces. Aborting cycle.");
            return {.success = false, .error = "Could not find active workspace in monitor workspaces"};
        }

        index += delta;
        if (index < 0) {
            if (nowrap) {
                return {.success = true, .error = ""}; // null operation because wrapping is disabled
            }
            index = getMonitorMaxWorkspaces(monitor->m_name) - 1; // wrap around to the last workspace
        }
        else if (index >= getMonitorMaxWorkspaces(monitor->m_name)) {
            if (nowrap) {
                return {.success = true, .error = ""}; // null operation because wrapping is disabled
            }
            index = 0; // wrap around to the first workspace
        }
        auto workspaceRef = g_pCompositor->getWorkspaceByName(workspaces[index]);
        if (workspaceRef.get() == nullptr) {
            // create it if it doesn't exist yet
            auto const workspaceID = getWorkspaceIDNameFromString(workspaces[index]).id;
            workspaceRef = g_pCompositor->createNewWorkspace(workspaceID, monitor->m_id);
        }
        monitor->changeWorkspace(workspaceRef, false, true, monitor != getCurrentMonitor());
    }
    return {.success = true, .error = ""};
}

static SDispatchResult splitCycleWorkspaces(const std::string& value)
{
    return cycleWorkspaces(value, !g_enableWrapping);
}

static SDispatchResult splitCycleWorkspacesNowrap(const std::string& value)
{
    Log::logger->log(Log::WARN, "[split-monitor-workspaces] split-cycleworkspacesnowrap is deprecated. Set the `enable_wrapping` config value to false instead.");
    raiseNotification("[split-monitor-workspaces] split-cycleworkspacesnowrap is deprecated. Set the `enable_wrapping` config value to false instead.");
    return cycleWorkspaces(value, true);
}

static SDispatchResult splitMoveToWorkspace(const std::string& workspace)
{
    if (!g_linkMonitors) {
        // not linked => just move to workspace on current monitor
        auto const result = dispatchMoveToWorkspace(getWorkspaceFromMonitor(getCurrentMonitor(), workspace), false);
        return {.success = result == "ok", .error = result};
    }
    // workspaces are linked => silently move to workspace, then change workspace on all monitors
    auto const result = dispatchMoveToWorkspace(getWorkspaceFromMonitor(getCurrentMonitor(), workspace), true);
    splitWorkspace(workspace);
    return {.success = result == "ok", .error = result};
}

static SDispatchResult splitMoveToWorkspaceSilent(const std::string& workspace)
{
    auto const result = dispatchMoveToWorkspace(getWorkspaceFromMonitor(getCurrentMonitor(), workspace), true);
    return {.success = result == "ok", .error = result};
}

static SDispatchResult changeMonitor(bool quiet, const std::string& value)
{
    PHLMONITOR monitor = getCurrentMonitor();

    PHLMONITOR nextMonitor = nullptr;

    uint64_t monitorCount = g_pCompositor->m_monitors.size();

    int const delta = getDelta(value);
    if (delta == 0) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Invalid monitor value: {}", value.c_str());
        return {.success = false, .error = "Invalid monitor value: " + value};
    }

    // The index is used instead of the monitorID because using the monitorID won't work if monitors are removed or mirrored
    // as there would be gaps in the monitorID sequence
    int currentMonitorIndex = -1;
    for (size_t i = 0; i < g_pCompositor->m_monitors.size(); i++) {
        if (g_pCompositor->m_monitors[i] == monitor) {
            currentMonitorIndex = i;
            break;
        }
    }
    if (currentMonitorIndex == -1) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Monitor ID {} not found in monitor list?", monitor->m_id);
        return {.success = false, .error = "Monitor ID not found in monitor list: " + std::to_string(monitor->m_id)};
    }

    int nextMonitorIndex = (monitorCount + currentMonitorIndex + delta) % monitorCount;

    nextMonitor = g_pCompositor->m_monitors[nextMonitorIndex];

    int nextWorkspaceID = nextMonitor->m_activeWorkspace->m_id;

    std::string result;
    if (quiet) {
        result = dispatchMoveToWorkspace(std::to_string(nextWorkspaceID), true);
    }
    else {
        result = dispatchMoveToWorkspace(std::to_string(nextWorkspaceID), false);
    }
    return {.success = result == "ok", .error = result};
}

static SDispatchResult splitChangeMonitorSilent(const std::string& value)
{
    return changeMonitor(true, value);
}

static SDispatchResult splitChangeMonitor(const std::string& value)
{
    return changeMonitor(false, value);
}

static SDispatchResult grabRogueWindows(const std::string& /*unused*/)
{
    // implementation loosely based on shezdy's hyprsplit: https://github.com/shezdy/hyprsplit
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Grabbing rogue windows");
    const auto currentMonitor = getCurrentMonitor();
    if (currentMonitor == nullptr) {
        Log::logger->log(Log::ERR, "[split-monitor-workspaces] No active monitor found");
        return {.success = false, .error = "No active monitor found"};
    }
    const auto currentWorkspace = currentMonitor->m_activeWorkspace;
    if (currentWorkspace == nullptr) {
        Log::logger->log(Log::ERR, "[split-monitor-workspaces] No active workspace found");
        return {.success = false, .error = "No active workspace found"};
    }

    for (const auto& window : g_pCompositor->m_windows) {
        // ignore unmapped and special windows
        if (!window->m_isMapped && !window->onSpecialWorkspace())
            continue;

        auto const workspaceName = window->m_workspace->m_name;
        auto const monitorID = window->m_monitor->m_id;

        bool isInRogueWorkspace = !g_vMonitorWorkspaceMap.contains(monitorID) || // if the monitor is not mapped, the window is rogue
                                  !std::ranges::any_of(g_vMonitorWorkspaceMap[monitorID], [&workspaceName](const auto& mappedWorkspaceName) { return workspaceName == mappedWorkspaceName; });
        if (isInRogueWorkspace) {
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Moving rogue window {} from workspace {} to workspace {}", window->m_title.c_str(), workspaceName.c_str(),
                             currentWorkspace->m_name.c_str());
            g_pCompositor->moveWindowToWorkspaceSafe(window, currentWorkspace);
        }
    }
    return {.success = true, .error = ""};
}

static int luaWorkspace(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitWorkspace(*arg)) : pushLuaArgError(L, "workspace");
}

static int luaCycleWorkspaces(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitCycleWorkspaces(*arg)) : pushLuaArgError(L, "cycle_workspaces");
}

static int luaCycleWorkspacesNowrap(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitCycleWorkspacesNowrap(*arg)) : pushLuaArgError(L, "cycle_workspaces_nowrap");
}

static int luaMoveToWorkspace(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitMoveToWorkspace(*arg)) : pushLuaArgError(L, "move_to_workspace");
}

static int luaMoveToWorkspaceSilent(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitMoveToWorkspaceSilent(*arg)) : pushLuaArgError(L, "move_to_workspace_silent");
}

static int luaChangeMonitor(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitChangeMonitor(*arg)) : pushLuaArgError(L, "change_monitor");
}

static int luaChangeMonitorSilent(lua_State* L)
{
    const auto arg = luaArgToString(L, 1);
    return arg ? pushLuaDispatchResult(L, splitChangeMonitorSilent(*arg)) : pushLuaArgError(L, "change_monitor_silent");
}

static int luaGrabRogueWindows(lua_State* L)
{
    return pushLuaDispatchResult(L, grabRogueWindows(""));
}

static int64_t calcWorkspaceBaseIndex(const std::string& name)
{
    int64_t currentPriority = g_vMonitorPriorities[name];

    int64_t offset = 0;
    for (const auto& [n, p] : g_vMonitorPriorities) {
        if (p < currentPriority) {
            offset += getMonitorMaxWorkspaces(n);
        }
    }

    return offset;
}

static void mapMonitor(const PHLMONITOR& monitor) // NOLINT(readability-convert-member-functions-to-static)
{
    if (monitor->m_activeMonitorRule.m_disabled) {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Skipping disabled monitor {}", monitor->m_name);
        return;
    }

    if (monitor->isMirror()) {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Skipping mirrored monitor {}", monitor->m_name);
        return;
    }

    // determine monitor priority if not set
    if (!g_vMonitorPriorities.contains(monitor->m_name)) {
        g_vMonitorPriorities[monitor->m_name] = static_cast<int64_t>(g_vMonitorPriorities.size());
    }

    int64_t workspaceIndex = calcWorkspaceBaseIndex(monitor->m_name);
    int64_t maxWorkspaces = getMonitorMaxWorkspaces(monitor->m_name);
    workspaceIndex += 1;

    Log::logger->log(Log::INFO, "{}",
                     "[split-monitor-workspaces] Mapping workspaces " + std::to_string(workspaceIndex) + "-" + std::to_string(workspaceIndex + maxWorkspaces - 1) + " to monitor " + monitor->m_name);

    for (int i = workspaceIndex; i < workspaceIndex + maxWorkspaces; i++) {
        std::string workspaceName = std::to_string(i);
        g_vMonitorWorkspaceMap[monitor->m_id].push_back(workspaceName);
        PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByName(workspaceName);

        // when not using persistent workspaces, we still want to create the first workspace on each monitor
        // to avoid issues where only the last mapped monitor has the correct workspace (#121)
        if (workspace.get() == nullptr && (g_enablePersistentWorkspaces || i == workspaceIndex)) {
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Creating workspace {}", workspaceName);
            workspace = g_pCompositor->createNewWorkspace(i, monitor->m_id);
        }
        if (workspace.get() != nullptr) {
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Moving workspace {} to monitor {}", workspaceName, monitor->m_name);
            g_pCompositor->moveWorkspaceToMonitor(workspace, monitor);
            if (g_enablePersistentWorkspaces) {
                workspace->setPersistent(true);
                g_vPersistentWorkspaces.push_back(workspace); // keep a reference to avoid it being destructed (see https://github.com/hyprwm/Hyprland/discussions/11400#discussioncomment-14085672)
            }
            else {
                // if this is the first workspace on the monitor, we still want to make sure it's focused on startup
                // this is to avoid issues where the workspace is destroyed right after we move it (#220)
                if (i == workspaceIndex && g_firstLoad) {
                    monitor->changeWorkspace(workspace, false, true, true);
                }
            }
        }
    }

    if (!g_keepFocused || g_firstLoad) {
        // we also want to switch to the first workspace when the plugin is first loaded
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Switching to first workspace {} on monitor {}", std::to_string(workspaceIndex), monitor->m_name);
        runHyprlandDispatcher("workspace", std::to_string(workspaceIndex));
    }
}

static void unmapMonitor(const PHLMONITOR& monitor)
{
    int64_t workspaceIndex = calcWorkspaceBaseIndex(monitor->m_name);
    int64_t maxWorkspaces = getMonitorMaxWorkspaces(monitor->m_name);
    workspaceIndex += 1;

    Log::logger->log(Log::INFO, "{}",
                     "[split-monitor-workspaces] Unmapping workspaces " + std::to_string(workspaceIndex) + "-" + std::to_string(workspaceIndex + maxWorkspaces - 1) + " from monitor " +
                         monitor->m_name);

    if (g_vMonitorWorkspaceMap.contains(monitor->m_id)) {
        for (const auto& workspaceName : g_vMonitorWorkspaceMap[monitor->m_id]) {
            PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByName(workspaceName);

            if (workspace.get() != nullptr) {
                workspace->setPersistent(false);
                // remove this workspace shared ptr from the persistent workspaces vector, so it can be destructed if no other references exist
                std::erase(g_vPersistentWorkspaces, workspace);
            }
        }
        g_vMonitorWorkspaceMap.erase(monitor->m_id);
    }

    if (g_vMonitorPriorities.contains(monitor->m_name) && !g_vMonitorPriorities[monitor->m_name].wasSetFromConfig) {
        g_vMonitorPriorities.erase(monitor->m_name);
    }

    if (g_vMonitorMaxWorkspaces.contains(monitor->m_name) && !g_vMonitorMaxWorkspaces[monitor->m_name].wasSetFromConfig) {
        g_vMonitorMaxWorkspaces.erase(monitor->m_name);
    }
}

static void unmapAllMonitors()
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Unmapping all monitors");
    while (!g_vMonitorWorkspaceMap.empty()) {
        auto [monitorID, workspaces] = *g_vMonitorWorkspaceMap.begin();
        PHLMONITOR monitor = g_pCompositor->getMonitorFromID(monitorID);
        if (monitor != nullptr) {
            unmapMonitor(monitor); // will remove the monitor from the map
        }
        else {
            g_vMonitorWorkspaceMap.erase(monitorID); // remove it manually
        }
    }
    g_vMonitorWorkspaceMap.clear();
    g_vPersistentWorkspaces.clear();
}

static void remapAllMonitors()
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Remapping all monitors");
    raiseNotification("[split-monitor-workspaces] Remapping workspaces...");
    unmapAllMonitors();
    for (const PHLMONITOR& monitor : g_pCompositor->m_monitors) {
        mapMonitor(monitor);
    }
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Mapped all monitors");
    // if keepFocused is false or first load, switch to the first workspace on the default or first monitor
    if (!g_keepFocused || g_firstLoad) {
        if (!g_pCompositor->m_monitors.empty()) {
            PHLMONITOR primaryMonitor = getPrimaryMonitor();
            if (primaryMonitor == nullptr) {
                Log::logger->log(Log::ERR, "[split-monitor-workspaces] No primary monitor found?");
                return;
            }
            if (!g_vMonitorWorkspaceMap.contains(primaryMonitor->m_id)) {
                Log::logger->log(Log::ERR, "[split-monitor-workspaces] Primary monitor ID {} not found in workspace map?", primaryMonitor->m_id);
                return;
            }
            if (!g_vMonitorWorkspaceMap[primaryMonitor->m_id].empty()) {
                std::string firstWorkspace = g_vMonitorWorkspaceMap[primaryMonitor->m_id][0];
                Log::logger->log(Log::INFO, "[split-monitor-workspaces] Switching to first workspace {} on first monitor {}", firstWorkspace, primaryMonitor->m_name);
                runHyprlandDispatcher("workspace", firstWorkspace);
            }
        }
        else {
            Log::logger->log(Log::ERR, "[split-monitor-workspaces] No monitors found?");
        }
    }
}

static void loadConfigValues()
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Loading config values");
    g_enableNotifications = getConfigValue<Hyprlang::INT>(configName(k_enableNotifications)) != 0;
    g_enablePersistentWorkspaces = getConfigValue<Hyprlang::INT>(configName(k_enablePersistentWorkspaces)) != 0;
    g_keepFocused = getConfigValue<Hyprlang::INT>(configName(k_keepFocused)) != 0;
    g_workspaceCount = getConfigValue<Hyprlang::INT>(configName(k_workspaceCount));
    g_enableWrapping = getConfigValue<Hyprlang::INT>(configName(k_enableWrapping)) != 0;
    g_defaultMonitor = getConfigValue<Config::STRING>(k_defaultMonitor);
    g_linkMonitors = getConfigValue<Hyprlang::INT>(configName(k_linkMonitors)) != 0;
    if (getConfigValue<Hyprlang::INT>(configName(k_enableHy3)) != 0) {
        g_hy3Status = Hy3Status::DETECTION_PENDING; // reset so it re-checks on next use
    }
    else {
        g_hy3Status = Hy3Status::DISABLED;
    }

    if (Config::mgr()->type() == Config::CONFIG_LUA) {
        loadMonitorPriority(getConfigValue<Config::STRING>(configName(k_monitorPriority)));
        loadMonitorMaxWorkspaces(getConfigValue<Config::STRING>(configName(k_monitorMaxWorkspaces)));
    }

    Log::logger->log(Log::INFO,
                     "[split-monitor-workspaces] Config values loaded: workspaceCount={}, keepFocused={}, enableNotifications={}, enablePersistentWorkspaces={}, enableWrapping={}, "
                     "defaultMonitor='{}', linkMonitors={}",
                     g_workspaceCount, g_keepFocused, g_enableNotifications, g_enablePersistentWorkspaces, g_enableWrapping, g_defaultMonitor.c_str(), g_linkMonitors);
}

static void reload()
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Reloading plugin configuration");
    loadConfigValues();
    remapAllMonitors();
    g_firstLoad = false;
}

static void monitorAddedCallback(const PHLMONITOR& monitor)
{ // NOLINT(performance-unnecessary-value-param)
    if (monitor == nullptr) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Monitor added callback called with nullptr?");
        return;
    }
    mapMonitor(monitor);
}

static void monitorRemovedCallback(PHLMONITOR monitor) // NOLINT(performance-unnecessary-value-param)
{
    if (monitor == nullptr) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Monitor removed callback called with nullptr?");
        return;
    }
    unmapMonitor(monitor);
}

static void configReloadedCallback() // NOLINT(performance-unnecessary-value-param)
{
    // !!! anything you call in this function should not reload the config, as it will cause an infinite loop !!!
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Config reloaded");
    raiseNotification("[split-monitor-workspaces] Config reloaded");
    reload();
}

static void preConfigReloadCallback() // NOLINT(performance-unnecessary-value-param)
{
    // clear monitor-specific config values. This is needed if the user
    // removes monitor_priority or monitor_max_workspaces entries from
    // the config. Without this, the old values would persist.
    g_vMonitorPriorities.clear();
    g_vMonitorMaxWorkspaces.clear();
}

static Hyprlang::CParseResult monitorPriorityConfigHandler(const char* command, const char* args)
{
    const auto ARGS = Hyprutils::String::CVarList2(args);

    int64_t priorityCounter = 0;
    for (const auto& arg : ARGS) {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Setting monitor priority: {} -> {}", arg, priorityCounter);
        g_vMonitorPriorities[std::string(arg)] = {.value = priorityCounter, .wasSetFromConfig = true};
        priorityCounter++;
    }

    Hyprlang::CParseResult result;
    return result;
}

static Hyprlang::CParseResult monitorMaxWorkspacesConfigHandler(const char* command, const char* args)
{
    const auto ARGS = Hyprutils::String::CVarList2(args);

    if (ARGS.size() != 2) {
        Hyprlang::CParseResult result;
        std::string errorMsg = "[split-monitor-workspaces] Invalid number of arguments, expected 2 (name, maxWorkspaces)";
        Log::logger->log(Log::ERR, errorMsg);
        result.setError(errorMsg.c_str());
        return result;
    }

    std::string parseError;

    try {
        auto const monitorName = std::string(ARGS[0]);
        const int maxWorkspaces = std::stoi(std::string(ARGS[1]));

        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Setting monitor max workspaces: {} -> {}", monitorName.c_str(), maxWorkspaces);
        g_vMonitorMaxWorkspaces[monitorName] = {.value = maxWorkspaces, .wasSetFromConfig = true};
    }
    catch (...) {
        parseError = "[split-monitor-workspaces] Failed to parse monitor max workspaces";
    }

    Hyprlang::CParseResult result;
    if (!parseError.empty()) {
        Log::logger->log(Log::ERR, parseError);
        result.setError(parseError.c_str());
    }
    return result;
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    PHANDLE = handle;

    HyprlandAPI::addConfigValueV2(PHANDLE, Config::Values::makeConfigValue<Config::Values::Int>(configName(k_workspaceCount), "How many workspaces to bind to each monitor", 10));
    HyprlandAPI::addConfigValueV2(
        PHANDLE, Config::Values::makeConfigValue<Config::Values::Int>(configName(k_keepFocused), "Keep current workspaces focused on plugin init/reload", 0));
    HyprlandAPI::addConfigValueV2(PHANDLE, Config::Values::makeConfigValue<Config::Values::Int>(configName(k_enableNotifications), "Enable plugin notifications", 0));
    HyprlandAPI::addConfigValueV2(
        PHANDLE, Config::Values::makeConfigValue<Config::Values::Int>(configName(k_enablePersistentWorkspaces), "Enable management of persistent workspaces", 1));
    HyprlandAPI::addConfigValueV2(PHANDLE, Config::Values::makeConfigValue<Config::Values::Int>(configName(k_enableWrapping), "Enable wrapping around workspaces", 1));
    HyprlandAPI::addConfigValueV2(PHANDLE, Config::Values::makeConfigValue<Config::Values::Int>(configName(k_linkMonitors), "Enable gnome-like workspace switching across monitors", 0));
    HyprlandAPI::addConfigValueV2(PHANDLE, Config::Values::makeConfigValue<Config::Values::Int>(configName(k_enableHy3), "Enable hy3 support", 1));

    if (Config::mgr()->type() == Config::CONFIG_LEGACY) {
        HyprlandAPI::addConfigKeyword(PHANDLE, k_monitorPriority, monitorPriorityConfigHandler, (Hyprlang::SHandlerOptions){.allowFlags = false});
        HyprlandAPI::addConfigKeyword(PHANDLE, k_monitorMaxWorkspaces, monitorMaxWorkspacesConfigHandler, (Hyprlang::SHandlerOptions){.allowFlags = false});
    }
    else {
        HyprlandAPI::addConfigValueV2(PHANDLE, Config::Values::makeConfigValue<Config::Values::String>(
            configName(k_monitorPriority), "Comma-separated monitor priority list for Lua config", ""));
        HyprlandAPI::addConfigValueV2(PHANDLE, Config::Values::makeConfigValue<Config::Values::String>(
            configName(k_monitorMaxWorkspaces), "Semicolon-separated 'MONITOR, COUNT' entries for Lua config", ""));
    }

    HyprlandAPI::addDispatcherV2(PHANDLE, "split-workspace", splitWorkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-cycleworkspaces", splitCycleWorkspaces);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-cycleworkspacesnowrap", splitCycleWorkspacesNowrap);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-movetoworkspace", splitMoveToWorkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-movetoworkspacesilent", splitMoveToWorkspaceSilent);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-changemonitor", splitChangeMonitor);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-changemonitorsilent", splitChangeMonitorSilent);
    HyprlandAPI::addDispatcherV2(PHANDLE, "split-grabroguewindows", grabRogueWindows);

    if (Config::mgr()->type() == Config::CONFIG_LUA) {
        HyprlandAPI::addLuaFunction(PHANDLE, k_luaNamespace, "workspace", luaWorkspace);
        HyprlandAPI::addLuaFunction(PHANDLE, k_luaNamespace, "cycle_workspaces", luaCycleWorkspaces);
        HyprlandAPI::addLuaFunction(PHANDLE, k_luaNamespace, "cycle_workspaces_nowrap", luaCycleWorkspacesNowrap);
        HyprlandAPI::addLuaFunction(PHANDLE, k_luaNamespace, "move_to_workspace", luaMoveToWorkspace);
        HyprlandAPI::addLuaFunction(PHANDLE, k_luaNamespace, "move_to_workspace_silent", luaMoveToWorkspaceSilent);
        HyprlandAPI::addLuaFunction(PHANDLE, k_luaNamespace, "change_monitor", luaChangeMonitor);
        HyprlandAPI::addLuaFunction(PHANDLE, k_luaNamespace, "change_monitor_silent", luaChangeMonitorSilent);
        HyprlandAPI::addLuaFunction(PHANDLE, k_luaNamespace, "grab_rogue_windows", luaGrabRogueWindows);
    }

    e_monitorAddedHandle = Event::bus()->m_events.monitor.added.listen(monitorAddedCallback);
    e_monitorRemovedHandle = Event::bus()->m_events.monitor.removed.listen(monitorRemovedCallback);
    e_configReloadedHandle = Event::bus()->m_events.config.reloaded.listen(configReloadedCallback);
    e_preConfigReloadHandle = Event::bus()->m_events.config.preReload.listen(preConfigReloadCallback);

    // config loading and initial mapping of the workspaces will happen after plugin initialization, through the configReloadedCallback.
    // this is because Hyprland will automatically force a config reload after the plugin is loaded

    raiseNotification("[split-monitor-workspaces] Initialized successfully!");
    return {"split-monitor-workspaces", "Split monitor workspace namespaces", "zjeffer", "1.2.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    unmapAllMonitors();
    raiseNotification("[split-monitor-workspaces] Unloaded successfully!");
}
