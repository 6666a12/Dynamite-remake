#pragma once
#include "gameplay_ui_config.hpp"
#include "gameplay_render_command.hpp"

struct ScreenRect {
    float x, y, w, h;
};

/**
 * 三侧局部坐标 → 屏幕坐标转换器
 *
 * 每侧轨道拥有独立的局部坐标系 (lane_pos, distance)：
 *   - lane_pos:  沿判定线的位置（对于 DOWN 是水平，对于 LEFT/RIGHT 是垂直）
 *   - distance:  垂直于判定线的距离（>0 表示 note 尚未到达判定线）
 *
 * 屏幕映射后，每个 note 得到一个轴对齐包围盒 (x, y, w, h)。
 */
class LaneCoordinateTransformer {
public:
    // 屏幕判定线位置（由使用者在 OnResize 时设置）
    float bottom_judge_y = 0.0f;
    float left_judge_x = 0.0f;
    float right_judge_x = 0.0f;
    float lane_center_y = 0.0f;     // 侧轨道沿判定线的中心 Y
    float bottom_center_x = 0.0f;   // 下轨道沿判定线的中心 X

    // 将 NoteRenderCommand 转换为屏幕 AABB
    ScreenRect TransformNote(const NoteRenderCommand& cmd) const;

    // 将特效位置转换为屏幕坐标
    void TransformEffect(const HitEffectCommand& cmd,
                         float& out_x, float& out_y) const;

private:
    void TransformBottom(float lane_pos, float distance,
                         float front_w, float front_t,
                         float& out_x, float& out_y,
                         float& out_w, float& out_h) const;

    void TransformLeft(float lane_pos, float distance,
                       float front_w, float front_t,
                       float& out_x, float& out_y,
                       float& out_w, float& out_h) const;

    void TransformRight(float lane_pos, float distance,
                        float front_w, float front_t,
                        float& out_x, float& out_y,
                        float& out_w, float& out_h) const;
};
