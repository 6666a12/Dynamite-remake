#include "tap_note_renderer.hpp"
#include "i_renderer.hpp"
#include "gameplay_ui_config.hpp"
#include "utils/texture.h"
#include <cmath>


static inline uint32_t ApplyAlpha(uint32_t color, float alpha) {
    uint8_t a = static_cast<uint8_t>(255.0f * alpha);
    return (color & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24);
}

TapNoteRenderer::TapNoteRenderer(std::shared_ptr<IRenderer> renderer)
    : renderer_(std::move(renderer)) {}

void TapNoteRenderer::Render(const NoteRenderCommand& cmd,
                              const LaneCoordinateTransformer& transformer) {
    ScreenRect sr = transformer.TransformNote(cmd);

    // 选择颜色
    uint32_t base_color;
    if (cmd.override_color.has_value()) {
        base_color = cmd.override_color.value();
    } else if (cmd.lane == LaneType::Down) {
        base_color = GameplayUI::CLR_NOTE_TAP;
    } else {
        base_color = GameplayUI::CLR_NOTE_SIDE_TAP;
    }
    uint32_t color = ApplyAlpha(base_color, cmd.alpha);

    // ---- 材质优先 ----
    const Texture* tap_tex = renderer_->GetNoteTapTex();
    if (tap_tex && tap_tex->valid()) {
        renderer_->DrawTexture(tap_tex, sr.x, sr.y, sr.w, sr.h, color);
        return;
    }

    // ---- 回退到圆角矩形 ----
    float radius = std::min(sr.w, sr.h) * 0.3f;
    radius = std::max(2.0f, radius);
    renderer_->DrawRoundedRect(sr.x, sr.y, sr.w, sr.h, radius, color);
}
