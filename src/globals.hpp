#pragma once

#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/////////////// Config key names ///////////////

inline constexpr auto k_workspaceCount = "plugin:split-monitor-workspaces:count";
inline constexpr auto k_keepFocused = "plugin:split-monitor-workspaces:keep_focused";
inline constexpr auto k_enableNotifications = "plugin:split-monitor-workspaces:enable_notifications";
inline constexpr auto k_enablePersistentWorkspaces = "plugin:split-monitor-workspaces:enable_persistent_workspaces";
inline constexpr auto k_enableWrapping = "plugin:split-monitor-workspaces:enable_wrapping";
inline constexpr auto k_defaultMonitor = "cursor:default_monitor";
inline constexpr auto k_monitorPriority = "plugin:split-monitor-workspaces:monitor_priority";
inline constexpr auto k_monitorMaxWorkspaces = "plugin:split-monitor-workspaces:max_workspaces";
inline constexpr auto k_linkMonitors = "plugin:split-monitor-workspaces:link_monitors";
inline constexpr auto k_enableHy3 = "plugin:split-monitor-workspaces:enable_hy3";

inline const CHyprColor s_pluginColor = {0x61 / 255.0F, 0xAF / 255.0F, 0xEF / 255.0F, 1.0F};

///////////////  Types ///////////////

// Hy3 explicit support
enum class Hy3Status : uint8_t {
    DISABLED,          // disabled in config
    DETECTION_PENDING, // not yet performed
    NOT_DETECTED,      // detection failed
    DETECTED,          // detection succeeded
};

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

struct SPluginConfig {
    SP<Config::Values::Int> workspaceCount;
    SP<Config::Values::Bool> keepFocused;
    SP<Config::Values::Bool> enableNotifications;
    SP<Config::Values::Bool> enablePersistentWorkspaces;
    SP<Config::Values::Bool> enableWrapping;
    SP<Config::Values::Bool> linkMonitors;
    SP<Config::Values::Bool> enableHy3;
};

///////////////  Global state ///////////////

inline HANDLE PHANDLE = nullptr;

inline SPluginConfig g_config;

inline std::string g_defaultMonitor;
inline Hy3Status g_hy3Status = Hy3Status::DETECTION_PENDING;

// the first time we load the plugin, we want to switch to the first workspace on the primary monitor regardless of keepFocused
inline bool g_firstLoad = true;

inline std::map<MONITORID, std::vector<std::string>> g_vMonitorWorkspaceMap;
inline std::vector<PHLWORKSPACE> g_vPersistentWorkspaces; // to keep ownership of persistent workspaces, otherwise Hyprland will remove them

inline std::map<std::string, MonitorConfigValue> g_vMonitorPriorities;
inline std::map<std::string, MonitorConfigValue> g_vMonitorMaxWorkspaces;

/////////////// Event handlers ///////////////

inline CHyprSignalListener e_monitorAddedHandle = nullptr;
inline CHyprSignalListener e_monitorRemovedHandle = nullptr;
inline CHyprSignalListener e_configReloadedHandle = nullptr;
inline CHyprSignalListener e_preConfigReloadHandle = nullptr; // triggered right before the config is reloaded
