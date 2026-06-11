#pragma once
#include <cstdint>

/**
 * GameplayUI —— UI 配置常量
 *
 * 所有值基于 1920x1080 设计分辨率。
 * 实际渲染时会经过 viewport 变换等比缩放 + 居中。
 */

// 颜色打包工具函数（必须在 struct 外定义为自由 constexpr 函数，
// 因为 GCC 16 对 class body 内的 constexpr 前向引用检查更严格）
constexpr uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r)
         | (static_cast<uint32_t>(g) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

struct GameplayUI {
    // ============================================================
    // 颜色（RGBA）
    // ============================================================

    // 难度标签
    static constexpr uint32_t CLR_DIFF_NORMAL = PackColor(68, 170, 68, 255);   // 绿色
    static constexpr uint32_t CLR_DIFF_HARD   = PackColor(255, 100, 50, 255);  // 橙红

    // 判定线
    static constexpr uint32_t CLR_JUDGE_LINE     = PackColor(80, 200, 255, 200);  // 浅蓝
    static constexpr uint32_t CLR_JUDGE_LINE_GLOW = PackColor(255, 255, 255, 100); // 白

    // Note 颜色
    static constexpr uint32_t CLR_NOTE_TAP       = PackColor(200, 230, 255, 255);  // 淡蓝
    static constexpr uint32_t CLR_NOTE_SIDE_TAP  = PackColor(200, 230, 255, 255);  // 淡蓝（侧轨同色）
    static constexpr uint32_t CLR_NOTE_HOLD_BODY  = PackColor(120, 180, 255, 200); // 蓝
    static constexpr uint32_t CLR_NOTE_HOLD_BORDER = PackColor(60, 140, 255, 255); // 深蓝

    // HUD
    static constexpr uint32_t CLR_SCORE       = PackColor(255, 255, 255, 255);  // 白色
    static constexpr uint32_t CLR_ACCURACY    = PackColor(180, 220, 255, 255);  // 淡蓝
    static constexpr uint32_t CLR_AP_COMBO    = PackColor(255, 215, 0, 255);    // 金色
    static constexpr uint32_t CLR_TEXT_PRIMARY = PackColor(220, 220, 220, 255); // 浅灰

    // 底部栏
    static constexpr uint32_t CLR_FOOTER_BG = PackColor(10, 10, 10, 200);      // 半透明黑

    // ============================================================
    // 尺寸
    // ============================================================
    // Note 基准（DOWN 轨道）
    static constexpr float NOTE_WIDTH_BASE      = 88.0f;
    static constexpr float NOTE_THICKNESS_BASE  = 18.0f;

    // 侧轨道 Note 宽度缩放
    static constexpr float SIDE_WIDTH_SCALE     = 0.6f;

    // Hold
    static constexpr float HOLD_ALPHA            = 0.3f;
    static constexpr float HOLD_BORDER_THICKNESS = 3.0f;

    // 判定线
    static constexpr float JUDGE_LINE_THICKNESS  = 4.0f;
    static constexpr float JUDGE_LINE_HIT_EXTRA  = 60.0f;

    // HUD
    static constexpr float HUD_HEIGHT     = 60.0f;
    static constexpr float HUD_PADDING_X  = 20.0f;
    static constexpr float HUD_ITEM_GAP   = 24.0f;

    // 底部信息栏
    static constexpr float FOOTER_HEIGHT         = 64.0f;
    static constexpr float DIFFICULTY_PILL_HEIGHT = 30.0f;
    static constexpr float DIFFICULTY_PILL_RADIUS = 15.0f;
};
