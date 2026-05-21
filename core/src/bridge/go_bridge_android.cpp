/**
 * Dynamite 重构项目 —— Android JNI 桥接层
 *
 * 职责：
 * - 作为 C++ GameCore 与 Android Java/Kotlin 层及 Go DataLayer 之间的桥梁
 * - 通过 JNI 调用 Java 方法，进而访问 gomobile 生成的 Go 库
 *
 * 当前为 stub 实现，用于 Phase 0~1 核心验证。
 * 真实数据层待后续链接 gomobile 库后替换。
 */

#include <nlohmann/json.hpp>
#include "bridge/go_bridge.h"
#include <SDL3/SDL.h>

#include <jni.h>
#include <android/log.h>
#include <string>
#include <filesystem>

#define LOG_TAG "GoBridgeAndroid"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 注意：JNI_OnLoad 由 SDL3 提供，此处不可重复定义。
// 如需 JavaVM，可通过 SDL 相关 API 获取。

/**
 * 获取 Android 内部存储路径（/data/data/<package>/files/）
 */
static std::string getInternalPath() {
    const char* path = SDL_GetAndroidInternalStoragePath();
    if (path) {
        return std::string(path) + "/";
    }
    return "/data/data/com.TunerGames.Dynamit/files/";
}

bool GoBridge::init(const std::string& dbPath) {
    (void)dbPath;
    LOGI("GoBridge::init —— Android stub mode");
    return true;
}

nlohmann::json GoBridge::loadUserConfig() {
    LOGI("GoBridge::loadUserConfig —— Android stub");
    return nlohmann::json{
        {"offset_ms", 0},
        {"note_speed", 1.3},
        {"skin_name", "default"}
    };
}

std::string GoBridge::getChartPath(const std::string& songID, const std::string& difficulty) {
    return getInternalPath() + "songs/" + songID + "/chart_" + difficulty + ".chart";
}

nlohmann::json GoBridge::getSongList() {
    LOGI("GoBridge::getSongList —— Android stub (single song hardcoded)");
    nlohmann::json songs = nlohmann::json::array();
    songs.push_back(nlohmann::json{
        {"id", "song_sample"},
        {"title", "Rosenkreuz†Vampir"},
        {"artist", "Unknown"},
        {"difficulties", nlohmann::json{
            {"HARD", 10},
            {"GIGA", 15}
        }}
    });
    return songs;
}

bool GoBridge::submitScore(const std::string& songID,
                           const std::string& diff,
                           const nlohmann::json& result) {
    (void)songID;
    (void)diff;
    (void)result;
    LOGI("GoBridge::submitScore —— Android stub");
    return false;
}

double GoBridge::getRValue() {
    LOGI("GoBridge::getRValue —— Android stub");
    return 0.0;
}
