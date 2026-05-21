#pragma once
#include <string>
#include <sstream>

// Windows headers may define ERROR macro, causing conflict
#ifdef ERROR
#undef ERROR
#endif

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    static void setLevel(LogLevel level);
    static void log(LogLevel level, const std::string& msg);

    template<typename... Args>
    static void debug(const std::string& fmt_str, Args&&... args) {
        log(LogLevel::DEBUG, formatImpl(fmt_str, std::forward<Args>(args)...));
    }
    template<typename... Args>
    static void info(const std::string& fmt_str, Args&&... args) {
        log(LogLevel::INFO, formatImpl(fmt_str, std::forward<Args>(args)...));
    }
    template<typename... Args>
    static void warn(const std::string& fmt_str, Args&&... args) {
        log(LogLevel::WARN, formatImpl(fmt_str, std::forward<Args>(args)...));
    }
    template<typename... Args>
    static void error(const std::string& fmt_str, Args&&... args) {
        log(LogLevel::ERROR, formatImpl(fmt_str, std::forward<Args>(args)...));
    }

private:
    static LogLevel current_level_;

    // 简单格式化：仅支持 {} 占位符（顺序替换）
    template<typename T, typename... Rest>
    static std::string formatImpl(const std::string& fmt, T&& val, Rest&&... rest) {
        size_t pos = fmt.find("{}");
        if (pos == std::string::npos) return fmt;
        std::ostringstream oss;
        oss << fmt.substr(0, pos) << val;
        return oss.str() + formatImpl(fmt.substr(pos + 2), std::forward<Rest>(rest)...);
    }
    static std::string formatImpl(const std::string& fmt) { return fmt; }
};
