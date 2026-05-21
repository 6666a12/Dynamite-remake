#include "lane_coordinate.hpp"
#include <algorithm>
#include <cmath>

ScreenRect LaneCoordinateTransformer::TransformNote(const NoteRenderCommand& cmd) const {
    ScreenRect r{};

    float effective_w = cmd.front_width * cmd.scale;
    float effective_t = cmd.front_thickness * cmd.scale;

    switch (cmd.lane) {
        case LaneType::Down:
            TransformBottom(cmd.lane_pos, cmd.distance,
                            effective_w, effective_t,
                            r.x, r.y, r.w, r.h);
            break;
        case LaneType::Left:
            // 侧轨道：宽度缩放，厚度不变
            TransformLeft(cmd.lane_pos, cmd.distance,
                          effective_w * GameplayUI::SIDE_WIDTH_SCALE,
                          effective_t,
                          r.x, r.y, r.w, r.h);
            break;
        case LaneType::Right:
            TransformRight(cmd.lane_pos, cmd.distance,
                           effective_w * GameplayUI::SIDE_WIDTH_SCALE,
                           effective_t,
                           r.x, r.y, r.w, r.h);
            break;
    }
    return r;
}

void LaneCoordinateTransformer::TransformEffect(const HitEffectCommand& cmd,
                                                 float& out_x, float& out_y) const {
    // 特效始终在判定线上：distance = 0
    // 用 TransformNote 的尺寸计算简化：只求位置
    switch (cmd.lane) {
        case LaneType::Down:
            out_x = bottom_center_x + cmd.lane_pos - 50.0f;  // 默认特效宽100
            out_y = bottom_judge_y - 40.0f;                   // 默认特效高80
            break;
        case LaneType::Left: {
            float tmp_x, tmp_y, tmp_w, tmp_h;
            TransformLeft(cmd.lane_pos, 0.0f, 100.0f, 80.0f, tmp_x, tmp_y, tmp_w, tmp_h);
            out_x = tmp_x;
            out_y = tmp_y;
            break;
        }
        case LaneType::Right: {
            float tmp_x, tmp_y, tmp_w, tmp_h;
            TransformRight(cmd.lane_pos, 0.0f, 100.0f, 80.0f, tmp_x, tmp_y, tmp_w, tmp_h);
            out_x = tmp_x;
            out_y = tmp_y;
            break;
        }
    }
}

// ---------- 下轨道（正面基准）----------
void LaneCoordinateTransformer::TransformBottom(
    float lane_pos, float distance,
    float w, float t,
    float& out_x, float& out_y,
    float& out_w, float& out_h) const
{
    out_w = w;           // 水平宽度
    out_h = t;           // 垂直厚度
    out_x = bottom_center_x + lane_pos - w * 0.5f;
    out_y = bottom_judge_y - distance - t * 0.5f;
}

// ---------- 左轨道（侧）----------
void LaneCoordinateTransformer::TransformLeft(
    float lane_pos, float distance,
    float w, float t,
    float& out_x, float& out_y,
    float& out_w, float& out_h) const
{
    // w = 沿判定线方向（垂直），已缩放
    // t = 垂直判定线方向（水平），不缩放
    out_w = t;           // 水平厚度（不缩放）
    out_h = w;           // 垂直宽度（已缩放）
    out_x = left_judge_x + distance - t * 0.5f;
    out_y = lane_center_y + lane_pos - w * 0.5f;
}

// ---------- 右轨道（侧，与左对称）----------
void LaneCoordinateTransformer::TransformRight(
    float lane_pos, float distance,
    float w, float t,
    float& out_x, float& out_y,
    float& out_w, float& out_h) const
{
    out_w = t;           // 水平厚度（不缩放）
    out_h = w;           // 垂直宽度（已缩放）
    out_x = right_judge_x - distance - t * 0.5f;
    out_y = lane_center_y + lane_pos - w * 0.5f;
}
