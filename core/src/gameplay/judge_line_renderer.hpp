#pragma once
#include <memory>

class IRenderer;

/**
 * 判定线渲染器
 *
 * 绘制三条判定线：
 *   - 下判定线 (Bottom)：水平线，y = bottom_y
 *   - 左判定线 (Left)：垂直线，x = left_x
 *   - 右判定线 (Right)：垂直线，x = right_x
 *
 * 样式：白色 3px 实线 + 外侧 8px 半透明光晕
 */
class JudgeLineRenderer {
public:
    explicit JudgeLineRenderer(std::shared_ptr<IRenderer> renderer);

    void Render(float screen_w, float screen_h,
                float bottom_y, float left_x, float right_x);

private:
    std::shared_ptr<IRenderer> renderer_;
};
