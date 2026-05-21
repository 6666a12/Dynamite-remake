/**
 * Dynamite 重构项目 —— iOS Objective-C++ 桥接层（Stub）
 *
 * 职责：
 * - 在 iOS 平台上连接 C++ GameCore 与 Go DataLayer（gomobile 生成 framework）
 * - 通过 Objective-C++ 调用 GoMobile.framework 中的导出函数
 *
 * TODO:
 * - 集成 GoMobile.framework 后，使用 extern "C" 链接 Go 导出函数
 * - 或通过 ObjC 桥接调用 Swift/ObjC 封装的数据层
 */

#include <nlohmann/json.hpp>
#include "bridge/go_bridge.h"

#import <Foundation/Foundation.h>
#include <string>
#include <iostream>

// -----------------------------------------------------------------------------
// GoBridge 接口实现（当前为空壳）
// -----------------------------------------------------------------------------

/**
 * 初始化 DataLayer
 *
 * TODO: 调用 GoMobile 导出函数或初始化本地 SQLite（iOS sandbox 路径）
 */
bool GoBridge::init(const std::string& dbPath) {
    (void)dbPath;
    NSLog(@"[GoBridge] init —— iOS stub 实现");
    return true;
}

/**
 * 加载用户配置
 *
 * TODO: 从 NSUserDefaults 或 Go DataLayer 读取配置
 */
nlohmann::json GoBridge::loadUserConfig() {
    NSLog(@"[GoBridge] loadUserConfig —— iOS stub 实现");
    return nlohmann::json{
        {"offset_ms", 0},
        {"note_speed", 1.3},
        {"skin_name", "default"}
    };
}

/**
 * 获取谱面路径
 *
 * TODO: 返回 iOS App Bundle 中 songs 资源目录的真实路径
 */
std::string GoBridge::getChartPath(const std::string& songID, const std::string& difficulty) {
    (void)songID;
    (void)difficulty;
    NSLog(@"[GoBridge] getChartPath —— iOS stub 实现");
    // 示例：未来可从 NSBundle mainBundle 获取路径
    // NSString* path = [[NSBundle mainBundle] pathForResource:@"chart_CASUAL" ofType:@"chart" inDirectory:[NSString stringWithFormat:@"songs/%s", songID.c_str()]];
    return "";
}

/**
 * 获取歌曲列表
 *
 * TODO: 从 Go DataLayer 获取歌曲元数据
 */
nlohmann::json GoBridge::getSongList() {
    NSLog(@"[GoBridge] getSongList —— iOS stub 实现");
    return nlohmann::json::array();
}

/**
 * 提交成绩
 *
 * TODO: 调用 Go DataLayer 的 SubmitScore，并处理 iCloud/GameCenter 同步
 */
bool GoBridge::submitScore(const std::string& songID,
                           const std::string& diff,
                           const nlohmann::json& result) {
    (void)songID;
    (void)diff;
    (void)result;
    NSLog(@"[GoBridge] submitScore —— iOS stub 实现");
    return false;
}

/**
 * 获取 R 值
 *
 * TODO: 从 Go DataLayer 获取玩家 rating
 */
double GoBridge::getRValue() {
    NSLog(@"[GoBridge] getRValue —— iOS stub 实现");
    return 0.0;
}
