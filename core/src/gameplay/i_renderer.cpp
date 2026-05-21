#include "i_renderer.hpp"
#include "engine/render_batch.h"
#include "utils/texture.h"

// ============================================================
// RenderBatchRenderer 实现
// ============================================================

void RenderBatchRenderer::DrawRect(float x, float y, float w, float h, uint32_t color) {
    batch_.submitRect(x, y, w, h, color);
}

void RenderBatchRenderer::DrawRoundedRect(float x, float y, float w, float h,
                                           float radius, uint32_t color) {
    batch_.submitRoundedRect(x, y, w, h, radius, color);
}

void RenderBatchRenderer::DrawTexture(const Texture* tex, float x, float y,
                                       float w, float h, uint32_t color,
                                       float uv_x, float uv_y,
                                       float uv_w, float uv_h) {
    batch_.submit(tex, x, y, w, h, color, 0.0f, uv_x, uv_y, uv_w, uv_h);
}

void RenderBatchRenderer::DrawText(const std::string& text, float x, float y,
                                    float scale, uint32_t color) {
    batch_.submitText(text, x, y, scale, color);
}

const Texture* RenderBatchRenderer::GetNoteTapTex() const {
    return batch_.getNoteTapTex();
}
const Texture* RenderBatchRenderer::GetNoteHoldTex() const {
    return batch_.getNoteHoldTex();
}
const Texture* RenderBatchRenderer::GetNoteHoldHeadTex() const {
    return batch_.getNoteHoldHeadTex();
}
const Texture* RenderBatchRenderer::GetNoteHoldPressedTex() const {
    return batch_.getNoteHoldPressedTex();
}
const Texture* RenderBatchRenderer::GetNoteHoldTailTex() const {
    return batch_.getNoteHoldTailTex();
}
const Texture* RenderBatchRenderer::GetNoteHoldZeroTex() const {
    return batch_.getNoteHoldZeroTex();
}
const Texture* RenderBatchRenderer::GetNoteSlideTex() const {
    return batch_.getNoteSlideTex();
}
const Texture* RenderBatchRenderer::GetEffectTex(const std::string& name, int index) const {
    return batch_.getEffectTex(name, index);
}

