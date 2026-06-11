#include "judge_line_renderer.hpp"
#include "i_renderer.hpp"
#include "gameplay_ui_config.hpp"


JudgeLineRenderer::JudgeLineRenderer(std::shared_ptr<IRenderer> renderer)
    : renderer_(std::move(renderer)) {}

void JudgeLineRenderer::Render(float screen_w, float screen_h,
                                float bottom_y, float left_x, float right_x) {
    const float thick = GameplayUI::JUDGE_LINE_THICKNESS;
    const float extra = GameplayUI::JUDGE_LINE_HIT_EXTRA;
    const uint32_t line_color = GameplayUI::CLR_JUDGE_LINE;
    const uint32_t glow_color = GameplayUI::CLR_JUDGE_LINE_GLOW;

    // ---- 下判定线（水平）----
    // 从 left_x 到 right_x
    float bottom_glow_y = bottom_y - extra;
    renderer_->DrawRect(left_x, bottom_glow_y,
                        right_x - left_x, thick + extra * 2.0f,
                        glow_color);
    renderer_->DrawRect(left_x, bottom_y - thick * 0.5f,
                        right_x - left_x, thick,
                        line_color);

    // ---- 左判定线（垂直）----
    renderer_->DrawRect(left_x - extra, 0.0f,
                        thick + extra * 2.0f, bottom_y,
                        glow_color);
    renderer_->DrawRect(left_x - thick * 0.5f, 0.0f,
                        thick, bottom_y,
                        line_color);

    // ---- 右判定线（垂直）----
    renderer_->DrawRect(right_x - extra, 0.0f,
                        thick + extra * 2.0f, bottom_y,
                        glow_color);
    renderer_->DrawRect(right_x - thick * 0.5f, 0.0f,
                        thick, bottom_y,
                        line_color);
}
