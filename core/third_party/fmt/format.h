#pragma once
#include <string>
#include <sstream>

/**
 * fmt 最小化存根（stub）
 * 
 * 项目中的 logger.h 依赖 fmt::format，但 third_party 未包含完整 fmt 库。
 * 此处提供一个仅支持字符串拼接的最小实现，使项目能正常编译。
 * 后续如需复杂格式化，可替换为官方 fmt 库。
 */
namespace fmt {

inline std::string format(const std::string& fmt_str) {
    return fmt_str;
}

template<typename T, typename... Args>
std::string format(const std::string& fmt_str, T&& value, Args&&... args) {
    // 极简实现：仅将第一个 {} 替换为 value，然后递归处理剩余参数
    size_t pos = fmt_str.find("{}");
    if (pos == std::string::npos) {
        return fmt_str;
    }
    std::ostringstream oss;
    oss << fmt_str.substr(0, pos);
    oss << value;
    oss << format(fmt_str.substr(pos + 2), std::forward<Args>(args)...);
    return oss.str();
}

} // namespace fmt
