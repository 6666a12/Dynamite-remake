#pragma once
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

class ConfigManager {
public:
    static ConfigManager& instance();
    void init(const std::string& config_path);
    void save();

    int screenWidth() const;
    int screenHeight() const;
    int refreshRate() const;

    void setScreenWidth(int w);
    void setScreenHeight(int h);
    void setRefreshRate(int rate);

    int offsetMs() const;
    float noteSpeed() const;
    void setOffsetMs(int ms);
    void setNoteSpeed(float speed);

private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    nlohmann::json data_;
    std::string filepath_;
};
