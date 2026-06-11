#include "hit_effect_renderer.hpp"
#include "i_renderer.hpp"
#include "utils/texture.h"
#include <algorithm>
#include <cmath>
#include "gameplay_ui_config.hpp"


static inline uint32_t ApplyAlpha(uint32_t color, float alpha) {
    uint8_t a = static_cast<uint8_t>(255.0f * std::max(0.0f, std::min(1.0f, alpha)));
    return (color & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24);
}

HitEffectRenderer::HitEffectRenderer(std::shared_ptr<IRenderer> renderer)
    : renderer_(std::move(renderer)) {}

void HitEffectRenderer::Render(const HitEffectCommand& cmd,
                                const LaneCoordinateTransformer& transformer) {
    float ex, ey;
    transformer.TransformEffect(cmd, ex, ey);

    float t = cmd.lifetime / cmd.max_lifetime; // 1.0 -> 0.0
    float scale = 1.0f + (1.0f - t) * 1.0f;    // 逐渐扩大
    float alpha = t;

    // 特效尺寸
    float ew = 100.0f * scale;
    float eh = 80.0f * scale;
    float center_x = ex + 50.0f;
    float center_y = ey + 40.0f;

    uint32_t base_color;
    switch (cmd.judgment_type) {
        case 0: base_color = PackColor(0, 255, 200, 255); break;  // Perfect: 青色
        case 1: base_color = PackColor(0, 200, 255, 255); break;  // Good: 蓝色
        default: base_color = PackColor(255, 50, 50, 255); break; // Miss: 红色
    }
    uint32_t color = ApplyAlpha(base_color, alpha);

    // 尝试使用特效纹理
    const char* eff_name = (cmd.judgment_type == 0) ? "tap" :
                           (cmd.judgment_type == 1) ? "hold" : "slide";
    int frame = static_cast<int>((1.0f - t) * 4.0f);
    frame = std::min(3, std::max(0, frame));

    const Texture* eff_tex = renderer_->GetEffectTex(eff_name, frame);
    if (eff_tex && eff_tex->valid()) {
        renderer_->DrawTexture(eff_tex,
                               center_x - ew * 0.5f,
                               center_y - eh * 0.5f,
                               ew, eh, color);
    } else {
        // 回退：绘制一个闪光矩形
        renderer_->DrawRect(center_x - ew * 0.5f,
                            center_y - eh * 0.5f,
                            ew, eh, color);
    }
}
