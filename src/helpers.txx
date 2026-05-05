// Template implementations, in a separate file to keep helpers.hpp clean.
#include "src/globals.hpp"

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprlang.hpp>

template <typename T> auto getConfigValue(const char* paramName)
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Getting config value {}", paramName);

    // TODO: remove call to deprecated getConfigValue function when fully transitioning to lua config system (sometime after v0.55 probably)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    void* const* dataPtr = HyprlandAPI::getConfigValue(PHANDLE, paramName)->getDataStaticPtr();
#pragma GCC diagnostic pop
    if (dataPtr == nullptr) {
        Log::logger->log(Log::ERR, "[split-monitor-workspaces] Config value {} not found", paramName);
        throw std::runtime_error("Config value not found: " + std::string(paramName));
    }

    if constexpr (std::is_same_v<T, Hyprlang::STRING>) {
        return *(const char**)dataPtr;
    }
    else {
        return *(T*)*dataPtr;
    }
}