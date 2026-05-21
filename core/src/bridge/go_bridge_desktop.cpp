/**
 * Dynamite 重构项目 —— 桌面端 GoBridge Mock 实现
 *
 * 功能：
 * - 在桌面开发环境中模拟 Go DataLayer 的行为
 * - 直接扫描本地 assets/songs/ 目录构建歌曲列表
 * - 将成绩保存到本地 JSON 文件
 *
 * 实现策略：
 * - 不依赖真实 Go 运行时，纯 C++ 实现
 * - 便于在 PC 上快速迭代 UI 和游戏逻辑
 */

#include <nlohmann/json.hpp>
#include "bridge/go_bridge.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <ctime>

namespace fs = std::filesystem;

/**
 * 初始化 GoBridge
 *
 * @param dbPath 数据库路径（桌面 mock 中可忽略，或作为日志记录）
 * @return 始终返回 true，表示初始化成功
 */
bool GoBridge::init(const std::string& dbPath) {
    // 桌面开发环境无需真实数据库连接
    // 如需调试 SQLite 可在此处尝试加载
    (void)dbPath;
    std::cout << "[GoBridge::init] 桌面 mock 模式已启动" << std::endl;
    return true;
}

/**
 * 加载用户配置
 *
 * @return 默认用户配置 JSON
 *   - offset_ms: 0（音频偏移，单位毫秒）
 *   - note_speed: 1.3（音符下落速度倍率）
 *   - skin_name: "default"（默认皮肤）
 */
nlohmann::json GoBridge::loadUserConfig() {
    return nlohmann::json{
        {"offset_ms", 0},
        {"note_speed", 1.3},
        {"skin_name", "default"}
    };
}

/**
 * 获取谱面文件路径
 *
 * @param songID 歌曲唯一标识
 * @param difficulty 难度标识（如 "CASUAL", "NORMAL" 等）
 * @return 拼接后的本地文件路径
 */
std::string GoBridge::getChartPath(const std::string& songID, const std::string& difficulty) {
    return "./assets/songs/" + songID + "/chart_" + difficulty + ".chart";
}

/**
 * 扫描本地歌曲目录，构建歌曲列表
 *
 * 扫描规则：
 * 1. 遍历 ./assets/songs/ 下的所有子目录
 * 2. 每个子目录视为一首歌曲的文件夹
 * 3. 尝试读取该目录下的 metadata.json
 * 4. 将成功解析的元数据加入返回列表
 *
 * @return 歌曲列表 JSON 数组
 */
nlohmann::json GoBridge::getSongList() {
    nlohmann::json songs = nlohmann::json::array();
    const fs::path songsDir = "./assets/songs";

    // 检查目录是否存在，防止遍历不存在的路径
    if (!fs::exists(songsDir) || !fs::is_directory(songsDir)) {
        std::cerr << "[GoBridge::getSongList] 歌曲目录不存在: " << songsDir << std::endl;
        return songs;
    }

    // 遍历歌曲目录下的所有子项
    for (const auto& entry : fs::directory_iterator(songsDir)) {
        if (!entry.is_directory()) {
            continue; // 跳过非目录项（如文件、符号链接等）
        }

        // 构造 metadata.json 路径
        fs::path metaPath = entry.path() / "metadata.json";
        if (!fs::exists(metaPath)) {
            continue; // 无元数据文件则跳过
        }

        try {
            std::ifstream ifs(metaPath);
            if (!ifs.is_open()) {
                continue;
            }
            nlohmann::json meta;
            ifs >> meta;

            // 校验必要字段是否存在，防止解析异常
            if (meta.contains("id") && meta.contains("title")) {
                songs.push_back(meta);
            }
        } catch (const std::exception& e) {
            // 单个文件解析失败不应阻断整个扫描流程
            std::cerr << "[GoBridge::getSongList] 解析失败: " << metaPath
                      << ", 原因: " << e.what() << std::endl;
        }
    }

    return songs;
}

/**
 * 提交成绩
 *
 * 桌面 mock 实现：将成绩追加写入本地 scores.json 文件
 *
 * @param songID 歌曲 ID
 * @param diff 难度
 * @param result 成绩详情 JSON
 * @return 保存成功返回 true
 */
bool GoBridge::submitScore(const std::string& songID,
                           const std::string& diff,
                           const nlohmann::json& result) {
    const std::string scoreFile = "./scores.json";

    nlohmann::json scores;
    // 尝试读取已有成绩文件
    try {
        std::ifstream ifs(scoreFile);
        if (ifs.is_open()) {
            ifs >> scores;
        }
    } catch (...) {
        // 文件不存在或解析失败则使用空数组
        scores = nlohmann::json::array();
    }

    if (!scores.is_array()) {
        scores = nlohmann::json::array();
    }

    // 构建成绩记录
    nlohmann::json record;
    record["song_id"] = songID;
    record["difficulty"] = diff;
    record["result"] = result;
    record["submitted_at"] = std::time(nullptr); // Unix 时间戳（秒）

    scores.push_back(record);

    // 写回文件
    try {
        std::ofstream ofs(scoreFile);
        if (!ofs.is_open()) {
            std::cerr << "[GoBridge::submitScore] 无法打开成绩文件: " << scoreFile << std::endl;
            return false;
        }
        ofs << scores.dump(4); // 美化缩进，便于人工查看
        std::cout << "[GoBridge::submitScore] 成绩已保存: " << songID << " [" << diff << "]" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[GoBridge::submitScore] 保存失败: " << e.what() << std::endl;
        return false;
    }
}

/**
 * 获取 R 值（排名值）
 *
 * TODO: 预留接口，未来对接 rating 算法（如基于 B50 的计算）
 * @return 当前始终返回 0.0
 */
double GoBridge::getRValue() {
    return 0.0;
}
