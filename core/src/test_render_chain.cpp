/**
 * Dynamite — 渲染链单元测试 (匹配实际 API)
 */
#include <catch2/catch_amalgamated.hpp>

#include "gameplay/lane_coordinate.hpp"
#include "gameplay/gameplay_ui_config.hpp"
#include "gameplay/gameplay_render_command.hpp"
#include "engine/chart_parser.h"

#include <cmath>

// ============================================================================
// PackColor 测试
// ============================================================================

TEST_CASE("PackColor: 基本打包") {
    REQUIRE(PackColor(255, 255, 255, 255) == 0xFFFFFFFF);
    REQUIRE(PackColor(255, 0, 0, 255) == 0xFF0000FF);
    REQUIRE(PackColor(0, 255, 0, 255) == 0xFF00FF00);
    REQUIRE(PackColor(0, 0, 255, 255) == 0xFFFF0000);
    REQUIRE(PackColor(0, 0, 0, 0) == 0x00000000);
    REQUIRE(PackColor(255, 255, 255, 128) == 0x80FFFFFF);
}

TEST_CASE("PackColor: 边界值") {
    REQUIRE(PackColor(0, 0, 0, 0) == 0);
    REQUIRE(PackColor(255, 255, 255, 255) == 0xFFFFFFFF);
}

// ============================================================================
// GameplayUI 常量测试
// ============================================================================

TEST_CASE("GameplayUI: 颜色常量非零") {
    REQUIRE(GameplayUI::CLR_DIFF_NORMAL != 0);
    REQUIRE(GameplayUI::CLR_DIFF_HARD != 0);
    REQUIRE(GameplayUI::CLR_JUDGE_LINE != 0);
    REQUIRE(GameplayUI::CLR_NOTE_TAP != 0);
    REQUIRE(GameplayUI::CLR_SCORE != 0);
}

TEST_CASE("GameplayUI: 尺寸常量为正") {
    REQUIRE(GameplayUI::NOTE_WIDTH_BASE > 0);
    REQUIRE(GameplayUI::NOTE_THICKNESS_BASE > 0);
    REQUIRE(GameplayUI::JUDGE_LINE_THICKNESS > 0);
    REQUIRE(GameplayUI::HUD_HEIGHT > 0);
    REQUIRE(GameplayUI::FOOTER_HEIGHT > 0);
}

TEST_CASE("GameplayUI: Hold 常量合理") {
    REQUIRE(GameplayUI::HOLD_ALPHA > 0.0f);
    REQUIRE(GameplayUI::HOLD_ALPHA <= 1.0f);
    REQUIRE(GameplayUI::HOLD_BORDER_THICKNESS > 0);
    REQUIRE(GameplayUI::SIDE_WIDTH_SCALE > 0.0f);
    REQUIRE(GameplayUI::SIDE_WIDTH_SCALE < 1.0f);
}

// ============================================================================
// LaneCoordinate 测试
// ============================================================================

TEST_CASE("LaneCoordinate: 默认值") {
    LaneCoordinateTransformer t;
    REQUIRE(t.bottom_judge_y == 0.0f);
    REQUIRE(t.left_judge_x == 0.0f);
    REQUIRE(t.right_judge_x == 0.0f);
    REQUIRE(t.lane_center_y == 0.0f);
    REQUIRE(t.bottom_center_x == 0.0f);
}

TEST_CASE("LaneCoordinate: 设置判定线位置") {
    LaneCoordinateTransformer t;
    t.bottom_judge_y = 945.0f;
    t.left_judge_x = 108.0f;
    t.right_judge_x = 1812.0f;
    t.lane_center_y = 540.0f;
    t.bottom_center_x = 960.0f;
    
    REQUIRE(t.bottom_judge_y == 945.0f);
    REQUIRE(t.left_judge_x == 108.0f);
    REQUIRE(t.right_judge_x == 1812.0f);
}

TEST_CASE("LaneCoordinate: TransformNote 基本调用") {
    LaneCoordinateTransformer t;
    t.bottom_judge_y = 945.0f;
    t.left_judge_x = 108.0f;
    t.right_judge_x = 1812.0f;
    t.bottom_center_x = 960.0f;
    t.lane_center_y = 540.0f;
    
    NoteRenderCommand cmd;
    cmd.lane = LaneType::Down;
    cmd.lane_pos = 0.5f;
    cmd.distance = 0.0f;
    cmd.front_width = 120.0f;
    cmd.front_thickness = 24.0f;
    
    ScreenRect r = t.TransformNote(cmd);
    REQUIRE(r.w >= 0);  // Width should be non-negative
    REQUIRE(r.h >= 0);
}

TEST_CASE("LaneCoordinate: TransformEffect 基本调用") {
    LaneCoordinateTransformer t;
    t.bottom_judge_y = 945.0f;
    t.bottom_center_x = 960.0f;
    t.lane_center_y = 540.0f;
    
    HitEffectCommand cmd;
    cmd.lane = LaneType::Down;
    cmd.lane_pos = 0.5f;
    cmd.distance = 0.0f;
    
    float x, y;
    t.TransformEffect(cmd, x, y);
    // Should produce valid coordinates
    REQUIRE(std::isfinite(x));
    REQUIRE(std::isfinite(y));
}

// ============================================================================
// NoteRenderCommand 测试
// ============================================================================

TEST_CASE("NoteRenderCommand: 默认值") {
    NoteRenderCommand cmd;
    REQUIRE(cmd.id == 0);
    REQUIRE(cmd.lane == LaneType::Down);
    REQUIRE(cmd.note_type == NoteType::TAP);
    REQUIRE(cmd.judge_state == 0);
    REQUIRE(cmd.lane_pos == 0.0f);
    REQUIRE(cmd.distance == 0.0f);
    REQUIRE(cmd.alpha == 1.0f);
    REQUIRE(cmd.scale == 1.0f);
}

TEST_CASE("NoteRenderCommand: override_color 初始为空") {
    NoteRenderCommand cmd;
    REQUIRE_FALSE(cmd.override_color.has_value());
}

TEST_CASE("HitEffectCommand: 默认值") {
    HitEffectCommand cmd;
    REQUIRE(cmd.lane == LaneType::Down);
    REQUIRE(cmd.judgment_type == 0);
    REQUIRE(cmd.max_lifetime == 0.3f);
}

// ============================================================================
// 枚举值测试
// ============================================================================

TEST_CASE("LaneType 枚举值") {
    REQUIRE(static_cast<uint8_t>(LaneType::Left) == 0);
    REQUIRE(static_cast<uint8_t>(LaneType::Down) == 1);
    REQUIRE(static_cast<uint8_t>(LaneType::Right) == 2);
}

TEST_CASE("NoteType 枚举值") {
    REQUIRE(static_cast<uint8_t>(NoteType::TAP) == 0);
    REQUIRE(static_cast<uint8_t>(NoteType::HOLD_HEAD) == 1);
    REQUIRE(static_cast<uint8_t>(NoteType::HOLD_BODY) == 2);
    REQUIRE(static_cast<uint8_t>(NoteType::HOLD_TAIL) == 3);
    REQUIRE(static_cast<uint8_t>(NoteType::SLIDE) == 4);
    REQUIRE(static_cast<uint8_t>(NoteType::MULTI) == 5);
}
