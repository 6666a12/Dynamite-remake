#pragma once
#include <string>
#include <optional>

// 使用 json_fwd.hpp 进行正确的前向声明
// json 在 nlohmann 库中为 basic_json<> 的别名，不可直接用 class 前向声明
#include <nlohmann/json_fwd.hpp>

/**
 * C++ 调用 Go DataLayer 的统一接口
 * 
 * 平台实现：
 * - Android: go_bridge_android.cpp (JNI)
 * - iOS: go_bridge_ios.mm (ObjC++)
 * - Desktop: go_bridge_desktop.cpp (直接链接 Go 静态库或 mock)
 */
namespace GoBridge {
    bool init(const std::string& dbPath);
    nlohmann::json loadUserConfig();
    std::string getChartPath(const std::string& songID, const std::string& difficulty);
    nlohmann::json getSongList();
    bool submitScore(const std::string& songID, const std::string& diff, const nlohmann::json& result);
    double getRValue();
}
