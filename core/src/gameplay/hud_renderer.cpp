#include "hud_renderer.hpp"
#include "i_renderer.hpp"
#include "gameplay_ui_config.hpp"
#include <cstdio>


HudRenderer::HudRenderer(std::shared_ptr<IRenderer> renderer)
    : renderer_(std::move(renderer)) {}

void HudRenderer::Render(const GameplayRenderFrame& frame, float screen_w) {
    const float margin = GameplayUI::HUD_PADDING_X;
    const float top_y = margin;

    // ---- 顶部 HUD 统计条 ----
    float top_bar_h = GameplayUI::HUD_HEIGHT;
    renderer_->DrawRect(0.0f, 0.0f, screen_w, top_bar_h,
                        PackColor(15, 15, 15, 200));

    float tag_h = 32.0f;
    float tag_gap = GameplayUI::HUD_ITEM_GAP;
    float tag_x = margin;

    // PERFECT
    DrawStatTag("PERFECT", frame.perfect_count,
                PackColor(0, 200, 255, 180),
                PackColor(0, 255, 200, 255),
                tag_x, top_y, tag_h);
    tag_x += 150.0f + tag_gap;

    // GOOD
    DrawStatTag("GOOD", frame.good_count,
                PackColor(255, 170, 0, 180),
                PackColor(255, 200, 0, 255),
                tag_x, top_y, tag_h);
    tag_x += 150.0f + tag_gap;

    // MISS
    DrawStatTag("MISS", frame.miss_count,
                PackColor(255, 68, 68, 180),
                PackColor(255, 100, 100, 255),
                tag_x, top_y, tag_h);

    // ---- 准确率 ----
    char acc_str[32];
    std::snprintf(acc_str, sizeof(acc_str), "%.2f%%", frame.accuracy);
    float acc_x = screen_w - margin - 180.0f;
    renderer_->DrawText("ACC", acc_x, top_y + 2.0f, 0.7f,
                        GameplayUI::CLR_ACCURACY);
    renderer_->DrawText(acc_str, acc_x + 50.0f, top_y, 0.9f,
                        GameplayUI::CLR_ACCURACY, true);

    // ---- Combo ----
    float left_x = margin;
    float combo_y = 1080.0f * 0.35f; // 暂用固定设计分辨率比例
    if (frame.combo > 0) {
        renderer_->DrawText("COMBO", left_x, combo_y, 0.7f,
                            PackColor(255, 255, 255, 180));
        char combo_str[16];
        std::snprintf(combo_str, sizeof(combo_str), "%d", frame.combo);
        renderer_->DrawText(combo_str, left_x, combo_y + 40.0f, 1.4f,
                            GameplayUI::CLR_AP_COMBO, true);
    }

    // ---- 暂停图标 ----
    float pause_x = screen_w * 0.5f - 20.0f;
    float pause_y = 1080.0f * 0.15f;
    renderer_->DrawRect(pause_x, pause_y, 6.0f, 28.0f,
                        PackColor(255, 255, 255, 200));
    renderer_->DrawRect(pause_x + 14.0f, pause_y, 6.0f, 28.0f,
                        PackColor(255, 255, 255, 200));

    // ---- 分数 ----
    float right_x = screen_w - margin - 260.0f;
    float score_y = 1080.0f * 0.22f;
    renderer_->DrawText("SCORE", right_x + 60.0f, score_y, 0.7f,
                        PackColor(255, 255, 255, 180));
    char score_str[16];
    std::snprintf(score_str, sizeof(score_str), "%07lld",
                  static_cast<long long>(frame.score));
    renderer_->DrawText(score_str, right_x, score_y + 40.0f, 1.4f,
                        GameplayUI::CLR_SCORE);
}

void HudRenderer::DrawStatTag(const char* label, int value, uint32_t bg_color,
                               uint32_t txt_color, float x, float y, float tag_h) {
    float label_w = 80.0f;
    renderer_->DrawRoundedRect(x, y, label_w + 60.0f, tag_h, 4.0f, bg_color);
    renderer_->DrawText(label, x + 6.0f, y + 5.0f, 0.6f, txt_color);
    char val_str[16];
    std::snprintf(val_str, sizeof(val_str), "%d", value);
    renderer_->DrawText(val_str, x + label_w, y + 2.0f, 0.8f,
                        GameplayUI::CLR_TEXT_PRIMARY, true);
}
