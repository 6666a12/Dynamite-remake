#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <optional>
#include <unordered_map>
#include "judge_engine.h"

/**
 * 谱面解析器 —— 原生支持 DynaMaker 社区谱面格式
 *
 * 支持的格式：
 * 1. DynaMaker XML (.xml)           - CMap/CMapNoteAsset 序列化
 * 2. DynaMaker ZIP Package (.zip)   - 社区分发标准包
 * 3. DynaMaker Web JSON (.json)     - 网页版导出格式
 *
 * 设计原则：直接兼容社区生态，不发明自定义二进制格式。
 */

// ========== 社区包元数据 ==========
struct DynaMakerPackage {
    std::string music_name;
    std::string music_composer;
    std::string noter_name;
    std::string music_file;
    std::string cover_file;
    std::string preview_file;
    std::unordered_map<std::string, std::string> difficulties;
};

// ========== 音符类型 ==========
enum class NoteType : uint8_t {
    TAP       = 0,
    HOLD_HEAD = 1,
    HOLD_BODY = 2,
    HOLD_TAIL = 3,
    SLIDE     = 4,
    MULTI     = 5
};

// ========== 音符数据 ==========
struct NoteData {
    uint32_t   id;
    NoteType   type;
    double     time_sec;
    uint64_t   time_ms;
    SideType   side;
    float      position;
    float      width;
    uint32_t   sub_id;
    uint64_t   duration_ms;
    uint32_t   flags;
    uint32_t   next_id;
    char       reserved[8];
};

// ========== BPM 事件 ==========
struct BPMEvent {
    double   time_sec;
    uint64_t time_ms;
    float    bpm;
    uint32_t beat_num;
    uint32_t beat_den;
};

// ========== 谱面 ==========
struct Chart {
    std::string song_id;
    std::string difficulty;
    std::string map_id;
    float       bpm;
    double      offset_sec;
    std::string left_region;
    std::string right_region;
    std::vector<NoteData> notes;
    std::vector<BPMEvent> bpm_events;
    int64_t  note_count;
    uint64_t duration_ms;
    float    chart_constant;
};

class ChartParser {
public:
    // ZIP 包解析
    static std::optional<std::vector<Chart>> parsePackage(const std::string& zip_path);
    static std::optional<DynaMakerPackage> parsePackageInfo(const std::string& zip_path);

    // XML 解析
    static std::optional<Chart> parseXmlFile(const std::string& file_path);
    static std::optional<Chart> parseXmlString(const std::string& xml_str);

    // JSON 解析
    static std::optional<Chart> parseDynaMakerJson(const std::string& json_str);

    // 统一入口（自动检测扩展名）
    static std::optional<Chart> parse(const std::string& path);

    // ============================================================
    // .chart 二进制格式：序列化 / 反序列化 / 缓存路径推断
    // ============================================================

    /// 魔数：DYNT (DynaNote Track)
    static constexpr uint32_t kMagic = 0x544E5944; // 'DYNT' little-endian

    /// 当前二进制版本
    static constexpr uint32_t kVersion = 3;

    /// 将 Chart 序列化为 .chart 二进制文件
    static bool writeChart(const Chart& chart, const std::string& path);

    /// 从 .chart 二进制文件反序列化出 Chart
    static std::optional<Chart> readChart(const std::string& path);

    /// 推断 .chart 缓存路径：将原始文件路径（.xml/.json）的扩展名替换为 .chart
    static std::string cachePathFrom(const std::string& original_path);

    /// 智能解析：优先读 .chart 缓存，若不存在或无效则解析原始文件并写入缓存
    static std::optional<Chart> parseWithCache(const std::string& path);

private:
    static double beatToSec(double beat_time, float bpm, double offset);
    static bool validateBounds(const Chart& chart);
};

