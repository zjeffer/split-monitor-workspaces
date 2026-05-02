// Template implementations, in a separate file to keep helpers.hpp clean.

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/debug/log/Logger.hpp>

template <typename T> auto getConfigValue(const char* paramName)
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