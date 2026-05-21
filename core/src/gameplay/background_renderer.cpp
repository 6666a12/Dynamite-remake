#include "background_renderer.hpp"
#include "i_renderer.hpp"
#include "utils/texture.h"

static inline uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r)
         | (static_cast<uint32_t>(g) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

BackgroundRenderer::BackgroundRenderer(std::shared_ptr<IRenderer> renderer)
    : renderer_(std::move(renderer)) {}

void BackgroundRenderer::Render(const Texture* cover_tex, float darken,
                                 float screen_w, float screen_h) {
    if (cover_tex && cover_tex->valid()) {
        // 绘制封面铺满全屏
        renderer_->DrawTexture(cover_tex, 0.0f, 0.0f,
                               screen_w, screen_h,
                               PackColor(255, 255, 255, 255));
        // 叠加暗化遮罩
        uint8_t dark_a = static_cast<uint8_t>(255.0f * darken);
        renderer_->DrawRect(0.0f, 0.0f, screen_w, screen_h,
                            PackColor(0, 0, 0, dark_a));
    } else {
        // 无封面时回退到纯色背景
        renderer_->DrawRect(0.0f, 0.0f, screen_w, screen_h,
                            PackColor(15, 15, 15, 255));
    }
}
