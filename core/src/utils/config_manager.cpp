#include "utils/config_manager.h"
#include "utils/logger.h"
#include <fstream>
#include <sstream>

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

void ConfigManager::init(const std::string& config_path) {
    filepath_ = config_path;
    std::ifstream ifs(filepath_);
    if (ifs.good()) {
        try {
            ifs >> data_;
            Logger::info("ConfigManager: 从 {} 加载配置", filepath_);
            return;
        } catch (const std::exception& e) {
            Logger::warn("ConfigManager: 配置解析失败 ({}), 使用默认值", e.what());
        }
    }
    // 文件不存在或解析失败：使用默认值
    data_ = nlohmann::json::parse(R"({
        "screen": {
            "width": 1920,
            "height": 1080,
            "refresh_rate": 60
        },
        "user": {
            "offset_ms": 0,
            "note_speed": 1.3
        },
        "play_mods": {
            "mirror": false,
            "bleed": false
        },
        "auto_play": false
    })");
    save();
    Logger::info("ConfigManager: 已创建默认配置文件 {}", filepath_);
}

void ConfigManager::save() {
    std::ofstream ofs(filepath_);
    if (ofs.good()) {
        ofs << data_.dump(2);
        Logger::info("ConfigManager: 配置已保存到 {}", filepath_);
    } else {
        Logger::error("ConfigManager: 无法写入 {}", filepath_);
    }
}

// ============================================================
// 屏幕参数
// ============================================================
int ConfigManager::screenWidth() const {
    return data_.value("screen", nlohmann::json::object()).value("width", 1920);
}
int ConfigManager::screenHeight() const {
    return data_.value("screen", nlohmann::json::object()).value("height", 1080);
}
int ConfigManager::refreshRate() const {
    return data_.value("screen", nlohmann::json::object()).value("refresh_rate", 60);
}

void ConfigManager::setScreenWidth(int w) {
    data_["screen"]["width"] = w;
}
void ConfigManager::setScreenHeight(int h) {
    data_["screen"]["height"] = h;
}
void ConfigManager::setRefreshRate(int rate) {
    data_["screen"]["refresh_rate"] = rate;
}

// ============================================================
// 用户设置
// ============================================================
int ConfigManager::offsetMs() const {
    return data_.value("user", nlohmann::json::object()).value("offset_ms", 0);
}
float ConfigManager::noteSpeed() const {
    return data_.value("user", nlohmann::json::object()).value("note_speed", 1.3f);
}

void ConfigManager::setOffsetMs(int ms) {
    data_["user"]["offset_ms"] = ms;
}
void ConfigManager::setNoteSpeed(float speed) {
    data_["user"]["note_speed"] = speed;
}

// ============================================================
// 演奏修改器
// ============================================================
bool ConfigManager::mirrorMod() const {
    return data_.value("play_mods", nlohmann::json::object()).value("mirror", false);
}
bool ConfigManager::bleedMod() const {
    return data_.value("play_mods", nlohmann::json::object()).value("bleed", false);
}
bool ConfigManager::autoPlay() const {
    return data_.value("auto_play", false);
}
void ConfigManager::setMirrorMod(bool v) {
    data_["play_mods"]["mirror"] = v;
}
void ConfigManager::setBleedMod(bool v) {
    data_["play_mods"]["bleed"] = v;
}
void ConfigManager::setAutoPlay(bool v) {
    data_["auto_play"] = v;
}
