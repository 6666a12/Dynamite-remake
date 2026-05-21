#pragma once
#include "gameplay_render_command.hpp"
#include <memory>

class IRenderer;

/**
 * HUD 渲染器
 *
 * 绘制顶部的判定统计（PERFECT / GOOD / MISS）、准确率、Combo、分数
 */
class HudRenderer {
public:
    explicit HudRenderer(std::shared_ptr<IRenderer> renderer);

    void Render(const GameplayRenderFrame& frame, float screen_w);

private:
    std::shared_ptr<IRenderer> renderer_;

    void DrawStatTag(const char* label, int value, uint32_t bg_color,
                     uint32_t txt_color, float x, float y, float tag_h);
};
