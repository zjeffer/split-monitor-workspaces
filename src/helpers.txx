// Template implementations, in a separate file to keep helpers.hpp clean.

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprlang.hpp>
#include <typeindex>

template <typename T> auto getConfigValue(const char* paramName)
{
    Log::logger->log(Log::INFO, "[split-monitor-workspaces] Getting config value {}", paramName);

    const auto reply = Config::mgr()->getConfigValue(paramName);
    if (!reply.dataptr || !reply.type) {
        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Failed to get config value {}", paramName);
        if constexpr (std::is_same_v<T, Config::STRING> || std::is_same_v<T, Hyprlang::STRING>)
            return std::string{};
        else
            return T{0};
    }

    void* const* VAL = reply.dataptr;
    const std::type_index TYPE = std::type_index(*reply.type);

    if constexpr (std::is_same_v<T, Config::STRING> || std::is_same_v<T, Hyprlang::STRING>) {
        if (TYPE == typeid(Config::STRING)) {
            const std::string* p = *reinterpret_cast<const std::string* const*>(VAL);
            if (!p)
                return std::string{};
            auto s = *p;
            if (s == STRVAL_EMPTY)
                s.clear();
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                s = s.substr(1, s.size() - 2);
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Got config value {}: {}", paramName, s);
            return s;
        }

        if (TYPE == typeid(Hyprlang::STRING) || TYPE == typeid(const char*)) {
            const char* p = *reinterpret_cast<const char* const*>(VAL);
            if (!p)
                return std::string{};
            std::string s = p;
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Got config value {}: {}", paramName, s);
            return s;
        }

        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Config value {} has unexpected type {}", paramName, reply.type->name());
        return std::string{};
    }
    else {
        if (TYPE == typeid(T)) {
            const T* p = *reinterpret_cast<const T* const*>(VAL);
            if (!p)
                return T{0};
            Log::logger->log(Log::INFO, "[split-monitor-workspaces] Got config value {}: {}", paramName, *p);
            return *p;
        }

        if (TYPE == typeid(Config::STRING)) {
            const std::string* p = *reinterpret_cast<const std::string* const*>(VAL);
            if (!p)
                return T{0};
            try {
                if constexpr (std::is_integral_v<T>)
                    return static_cast<T>(std::stoll(*p));
                else if constexpr (std::is_floating_point_v<T>)
                    return static_cast<T>(std::stod(*p));
            }
            catch (...) {
                return T{0};
            }
        }

        if (TYPE == typeid(Hyprlang::STRING) || TYPE == typeid(const char*)) {
            const char* p = *reinterpret_cast<const char* const*>(VAL);
            if (!p)
                return T{0};
            try {
                if constexpr (std::is_integral_v<T>)
                    return static_cast<T>(std::stoll(p));
                else if constexpr (std::is_floating_point_v<T>)
                    return static_cast<T>(std::stod(p));
            }
            catch (...) {
                return T{0};
            }
        }

        Log::logger->log(Log::WARN, "[split-monitor-workspaces] Type mismatch reading config {} (asked {}, got {}), returning default", paramName, typeid(T).name(), reply.type->name());
        return T{0};
    }
}