// Template implementations, in a separate file to keep helpers.hpp clean.

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/debug/log/Logger.hpp>

template <typename T> auto getConfigValue(const char* paramName)
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Getting config value {}", paramName);

    const auto reply = Config::mgr()->getConfigValue(paramName);
    if (reply.dataptr == nullptr || *reply.dataptr == nullptr) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Failed to get config value {}", paramName);
        if constexpr (std::is_same_v<T, Config::STRING> || std::is_same_v<T, const char*>)
            return std::string{};
        else
            return T{0};
    }

    const auto* const paramPtr = reinterpret_cast<const T*>(*reply.dataptr);
    if (paramPtr == nullptr) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Config value {} was null", paramName);
        if constexpr (std::is_same_v<T, Config::STRING> || std::is_same_v<T, const char*>)
            return std::string{};
        else
            return T{0};
    }

    if constexpr (std::is_same_v<T, Config::STRING> || std::is_same_v<T, const char*>) {
        const char* rawStr = nullptr;
        if constexpr (std::is_same_v<T, const char*>) {
            // Hyprlang::STRING = const char*, which stores empty strings as nullptr.
            // std::format("{}", nullptr) is UB and crashes, so guard before logging.
            rawStr = *paramPtr;
            if (rawStr == nullptr) {
                Log::logger->log(Log::DEBUG, "[split-monitor-workspaces] Config value {} is empty", paramName);
                return std::string{};
            }
        }
        else {
            rawStr = paramPtr->c_str();
        }
        auto paramStr = std::string{rawStr};
        // strip leading and trailing quotes if any (god I hate toml)
        if (paramStr.size() >= 2 && paramStr.front() == '"' && paramStr.back() == '"') {
            paramStr = paramStr.substr(1, paramStr.size() - 2);
        }
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Got config value {}: {}", paramName, paramStr);
        return paramStr;
    }
    else {
        Log::logger->log(Log::INFO, "[split-monitor-workspaces] Got config value {}: {}", paramName, *paramPtr);
        return *paramPtr;
    }
}