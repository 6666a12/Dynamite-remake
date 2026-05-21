#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

// NoteType 枚举来自 chart_parser
#include "../engine/chart_parser.h"

// ============================================================
// 统一轨道类型
// ============================================================
enum class LaneType : uint8_t {
    Left = 0,
    Down = 1,
    Right = 2
};

// ============================================================
// Note 渲染指令（由上层谱面系统提供）
// ============================================================
struct NoteRenderCommand {
    int32_t id = 0;
    LaneType lane = LaneType::Down;
    NoteType note_type = NoteType::TAP;  // 复用 chart_parser.h 的 NoteType
    uint8_t judge_state = 0; // 0=Pending, 1=Active, 2=Hit(Perfect), 3=Hit(Good), 4=Miss, 5=Holding

    // === 局部坐标（以判定线为原点）===
    float lane_pos = 0.0f;      // 沿判定线的位置
    float distance = 0.0f;      // 离判定线的距离（>0 表示尚未到达）

    // === 正面基准尺寸（下轨道参考值）===
    float front_width = 120.0f;         // 沿判定线方向基准宽度
    float front_thickness = 24.0f;      // 垂直判定线方向基准厚度

    // === Hold 专用 ===
    float hold_length_front = 0.0f;     // Hold 沿轨道方向长度（正面基准）
    float hold_progress = 0.0f;         // 当前按住进度 0.0~1.0

    // 材质覆盖（可选）
    std::optional<uint32_t> override_color;

    // 动画参数
    float alpha = 1.0f;
    float scale = 1.0f;
};

// ============================================================
// 判定特效指令
// ============================================================
struct HitEffectCommand {
    LaneType lane = LaneType::Down;
    float lane_pos = 0.0f;      // 沿判定线位置
    float distance = 0.0f;      // 判定发生时 distance = 0
    int32_t judgment_type = 0;  // 0=Perfect, 1=Good, 2=Miss
    float lifetime = 0.0f;
    float max_lifetime = 0.3f;
};

// ============================================================
// 每帧注入的完整渲染包
// ============================================================
struct GameplayRenderFrame {
    std::vector<NoteRenderCommand> notes;
    std::vector<HitEffectCommand> effects;

    std::string cover_texture_key;
    float cover_darken = 0.6f;
    float cover_blur_sigma = 4.0f;

    // HUD 数值
    int32_t perfect_count = 0;
    int32_t good_count = 0;
    int32_t miss_count = 0;
    float accuracy = 100.0f;
    int32_t max_combo = 0;
    int32_t combo = 0;
    int64_t score = 0;

    bool is_paused = false;

    std::string song_title;
    std::string difficulty_label;
    uint32_t difficulty_color = 0x3B82F6FF;
};
