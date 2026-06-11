#include "footer_renderer.hpp"
#include "i_renderer.hpp"
#include "gameplay_ui_config.hpp"


FooterRenderer::FooterRenderer(std::shared_ptr<IRenderer> renderer)
    : renderer_(std::move(renderer)) {}

void FooterRenderer::Render(const std::string& song_title,
                             const std::string& difficulty_label,
                             uint32_t difficulty_color,
                             float screen_w, float screen_h) {
    float info_h = GameplayUI::FOOTER_HEIGHT;
    float info_y = screen_h - info_h;

    // 底部背景
    renderer_->DrawRect(0.0f, info_y, screen_w, info_h,
                        GameplayUI::CLR_FOOTER_BG);

    // 上边框线
    renderer_->DrawRect(0.0f, info_y, screen_w, 1.0f,
                        PackColor(40, 40, 40, 255));

    const float margin = 24.0f;

    // 曲名（左对齐）
    if (!song_title.empty()) {
        renderer_->DrawText(song_title, margin, info_y + 20.0f, 0.9f,
                            GameplayUI::CLR_TEXT_PRIMARY);
    }

    // 难度标签（右侧）
    if (!difficulty_label.empty()) {
        float tag_w = 160.0f;
        float tag_h = GameplayUI::DIFFICULTY_PILL_HEIGHT;
        float tag_x = screen_w * 0.6f;
        float tag_y = info_y + (info_h - tag_h) * 0.5f;
        renderer_->DrawRoundedRect(tag_x, tag_y, tag_w, tag_h,
                                   GameplayUI::DIFFICULTY_PILL_RADIUS,
                                   difficulty_color);
        float text_x = tag_x + (tag_w - static_cast<float>(difficulty_label.size()) * 14.0f) * 0.5f;
        renderer_->DrawText(difficulty_label, text_x, tag_y + 4.0f, 0.9f,
                            GameplayUI::CLR_TEXT_PRIMARY);
    }
}
