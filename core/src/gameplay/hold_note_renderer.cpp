#include "hold_note_renderer.hpp"
#include "i_renderer.hpp"
#include "gameplay_ui_config.hpp"
#include "utils/texture.h"
#include <algorithm>
#include <cmath>

static constexpr float HOLD_SEG_PX = 180.0f;


static inline uint32_t ApplyAlpha(uint32_t color, float alpha) {
    uint8_t a = static_cast<uint8_t>(255.0f * std::max(0.0f, std::min(1.0f, alpha)));
    return (color & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24);
}

HoldNoteRenderer::HoldNoteRenderer(std::shared_ptr<IRenderer> renderer)
    : renderer_(std::move(renderer)) {}

// ============================================================
// 纹理平铺绘制
// 所有 hold 纹理统一为竖直方向（DOWN 轨道方向）。
// is_vertical: true=DOWN(竖直), false=LEFT/RIGHT(水平)
// reverse_uv: true=沿轨道方向反转（LEFT 需要，因为 LEFT 从右向左延伸）
// ============================================================
static void DrawTexturedSegmentInternal(IRenderer* r, const Texture* tex,
                                         float x, float y, float w, float h,
                                         uint32_t color,
                                         bool is_vertical, bool reverse_uv) {
    if (!tex || !tex->valid()) {
        r->DrawRect(x, y, w, h, color);
        return;
    }

    float total_len = is_vertical ? h : w;
    float cross_len = is_vertical ? w : h;
    float tex_h_f = static_cast<float>(tex->height());

    int num_full = static_cast<int>(total_len / HOLD_SEG_PX);
    float remainder = total_len - num_full * HOLD_SEG_PX;
    float cursor = 0.0f;

    for (int i = 0; i < num_full; ++i) {
        float sx, sy, sw, sh;
        float u, v, uw, vh;

        if (is_vertical) {
            sx = x; sy = y + cursor; sw = cross_len; sh = HOLD_SEG_PX;
            u = 0.0f; uw = 1.0f;
            v = reverse_uv ? (1.0f - HOLD_SEG_PX / tex_h_f) : 0.0f;
            vh = HOLD_SEG_PX / tex_h_f;
        } else {
            sx = x + cursor; sy = y; sw = HOLD_SEG_PX; sh = cross_len;
            u = reverse_uv ? (1.0f - HOLD_SEG_PX / tex_h_f) : 0.0f;
            uw = HOLD_SEG_PX / tex_h_f;
            v = 0.0f; vh = 1.0f;
        }

        r->DrawTexture(tex, sx, sy, sw, sh, color, u, v, uw, vh);
        cursor += HOLD_SEG_PX;
    }

    if (remainder > 1.0f) {
        float ratio = remainder / HOLD_SEG_PX;
        float sx, sy, sw, sh;
        float u, v, uw, vh;

        if (is_vertical) {
            sx = x; sy = y + cursor; sw = cross_len; sh = remainder;
            u = 0.0f; uw = 1.0f;
            v = reverse_uv ? (1.0f - ratio) : 0.0f;
            vh = ratio;
        } else {
            sx = x + cursor; sy = y; sw = remainder; sh = cross_len;
            u = reverse_uv ? (1.0f - ratio) : 0.0f;
            uw = ratio;
            v = 0.0f; vh = 1.0f;
        }

        r->DrawTexture(tex, sx, sy, sw, sh, color, u, v, uw, vh);
    }
}

// ============================================================
// 主渲染入口
//
// Hold 的视觉结构（以 DOWN 轨道为例，方向：上 = 屏幕顶部，下 = 判定线）：
//
//   ┌──────────────── tail (hold_tail.png, 箭头朝上)
//   │
//   │    hold body (hold.png, 竖直平铺纹理)
//   │
//   ├──────────────── head (hold_head.png, 箭头朝下)
//   │
//   ──────────────── 判定线
//   │
//   │    假身体 (仅 active_pending 态，hold.png 半透明)
//   │
//
// LEFT/RIGHT 同理，只是整体旋转 90° 并翻转方向。
// 所有纹理已统一为竖直方向，LEFT/RIGHT 通过 reverse_uv + 轴交换实现。
// ============================================================
void HoldNoteRenderer::Render(const NoteRenderCommand& cmd,
                               const LaneCoordinateTransformer& transformer) {
    // ---- 1. head 屏幕坐标 ----
    ScreenRect sr = transformer.TransformNote(cmd);

    uint32_t base_color = cmd.override_color.has_value()
        ? cmd.override_color.value()
        : GameplayUI::CLR_NOTE_HOLD_BODY;

    // ---- 2. 状态判断 ----
    bool is_judged_pass  = (cmd.judge_state == 2 || cmd.judge_state == 3);
    bool is_miss         = (cmd.judge_state == 4);
    bool is_holding      = (cmd.judge_state == 5);
    bool is_active       = (cmd.judge_state == 1);
    bool is_pending      = (cmd.judge_state == 0);

    // 判定通过后：无 head、无假身体，只剩下正在收起的真身体
    bool hide_head = is_judged_pass;
    bool hide_ghost = (is_judged_pass || is_holding);

    // ---- 零长度 hold（即 tap），用 UI0_Sprites_29.png 纹理 ----
    if (cmd.hold_length_front <= 0.0f) {
        uint32_t zero_color = ApplyAlpha(base_color, cmd.alpha);
        const Texture* zt = renderer_->GetNoteHoldZeroTex();
        if (zt && zt->valid())
            renderer_->DrawTexture(zt, sr.x, sr.y, sr.w, sr.h, zero_color);
        else {
            const Texture* ht = renderer_->GetNoteHoldHeadTex();
            if (ht && ht->valid())
                renderer_->DrawTexture(ht, sr.x, sr.y, sr.w, sr.h, zero_color);
            else
                renderer_->DrawRoundedRect(sr.x, sr.y, sr.w, sr.h,
                                           std::min(sr.w, sr.h) * 0.3f, zero_color);
        }
        // 零长度 hold 的判定特效由外部 GameplayRenderer 处理，此处不处理
        return;
    }

    // ---- 3. 计算总长度 ----
    bool is_vertical = (cmd.lane == LaneType::Down);
    float full_len = is_vertical
        ? cmd.hold_length_front * cmd.scale
        : cmd.hold_length_front * cmd.scale * GameplayUI::SIDE_WIDTH_SCALE;
    full_len = std::max(20.0f, full_len);

    // ---- 4. 计算折叠（真身体从判定线端向上收起）----
    // over_line_dist: head 越过判定线的距离（正值表示已过线）
    float over_line_dist = std::max(0.0f, -cmd.distance);
    bool is_past_judge = (over_line_dist > 0.0f);

    float shrink = 0.0f;
    if (is_past_judge && !is_pending) {
        shrink = std::min(1.0f, over_line_dist / std::max(full_len, 1.0f));
    }

    float true_len = full_len * (1.0f - shrink);
    float ghost_len = (!hide_ghost && is_past_judge && is_active) ? (full_len * shrink) : 0.0f;

    // ---- 5. 坐标轴 ----
    // 对于 DOWN: 从上到下延伸（tail→head），head 在下端
    // 对于 LEFT: 从右到左延伸，head 在左侧判定线端
    // 对于 RIGHT: 从左到右延伸，head 在右侧判定线端
    float head_axis, tail_axis;

    if (cmd.lane == LaneType::Down) {
        head_axis = sr.y + sr.h * 0.5f;
        tail_axis = head_axis - full_len;  // tail 在 head 上方
    } else if (cmd.lane == LaneType::Left) {
        head_axis = sr.x + sr.w * 0.5f;      // head 在左侧
        tail_axis = head_axis + full_len;     // tail 在右侧
    } else {
        head_axis = sr.x + sr.w * 0.5f;      // head 在右侧
        tail_axis = head_axis - full_len;     // tail 在左侧
    }

    // ---- 6. 真身体矩形 ----
    float tx = sr.x, ty = sr.y;
    float tw = sr.w, th = sr.h;
    if (cmd.lane == LaneType::Down) {
        ty = tail_axis; th = true_len;
    } else if (cmd.lane == LaneType::Left) {
        tx = tail_axis - sr.w; tw = true_len;
    } else {
        tx = tail_axis; tw = true_len;
    }

    // ---- 7. tail 端矩形（真身体起始端）----
    // 绘制在真身体的 tail 端（与 tail_axis 对齐的那一端）
    // 实际 head 在另一侧，tail 在尾部时，head 端按折叠缩短
    float tail_icon_size = is_vertical ? sr.w : sr.h;
    float tail_icon_x = tx, tail_icon_y = ty;
    float tail_icon_w = sr.w, tail_icon_h = sr.h;
    if (cmd.lane == LaneType::Down) {
        tail_icon_y = ty;
        tail_icon_h = tail_icon_size;
    } else if (cmd.lane == LaneType::Left) {
        // LEFT: tail 在右侧（x正方向）
        tail_icon_x = tx + tw - tail_icon_size;
        tail_icon_w = tail_icon_size;
    } else {
        // RIGHT: tail 在左侧（x负方向）
        tail_icon_x = tx;
        tail_icon_w = tail_icon_size;
    }

    // ---- 8. Miss 分支 ----
    if (is_miss) {
        uint32_t fade = ApplyAlpha(base_color, cmd.alpha * 0.3f);
        float mx = sr.x, my = sr.y;
        float mw = sr.w, mh = sr.h;
        if (cmd.lane == LaneType::Down) {
            mh = sr.h + full_len;
        } else if (cmd.lane == LaneType::Left) {
            mw = sr.w + full_len;
            mx = sr.x - full_len;
        } else {
            mw = sr.w + full_len;
        }
        DrawTexturedSegmentInternal(renderer_.get(), renderer_->GetNoteHoldTex(),
                                     mx, my, mw, mh, fade,
                                     is_vertical, cmd.lane == LaneType::Left);
        // head 图标
        const Texture* ht = renderer_->GetNoteHoldHeadTex();
        if (ht && ht->valid())
            renderer_->DrawTexture(ht, sr.x, sr.y, sr.w, sr.h, fade);
        else
            renderer_->DrawRoundedRect(sr.x, sr.y, sr.w, sr.h,
                                       std::min(sr.w, sr.h) * 0.3f, fade);
        return;
    }

    // ---- 9. 绘制真身体 ----
    uint32_t real_color = ApplyAlpha(base_color, cmd.alpha);
    if (true_len > 2.0f) {
        DrawTexturedSegmentInternal(renderer_.get(), renderer_->GetNoteHoldTex(),
                                     tx, ty, tw, th, real_color,
                                     is_vertical, cmd.lane == LaneType::Left);
    }

    // ---- 10. 绘制 tail 图标 ----
    if (true_len > 2.0f) {
        const Texture* tt = renderer_->GetNoteHoldTailTex();
        if (tt && tt->valid()) {
            renderer_->DrawTexture(tt, tail_icon_x, tail_icon_y,
                                   tail_icon_w, tail_icon_h, real_color);
        }
    }

    // ---- 11. 绘制假身体 ----
    if (is_active && ghost_len > 2.0f) {
        uint32_t ghost_color = ApplyAlpha(PackColor(255, 200, 100, 255),
                                           cmd.alpha * GameplayUI::HOLD_ALPHA);
        float gx = sr.x, gy = sr.y;
        float gw = sr.w, gh = sr.h;
        float judge_axis;
        if (cmd.lane == LaneType::Down) {
            judge_axis = transformer.bottom_judge_y;
            gy = judge_axis; gh = ghost_len;
        } else if (cmd.lane == LaneType::Left) {
            judge_axis = transformer.left_judge_x;
            gx = judge_axis - ghost_len; gw = ghost_len;
        } else {
            judge_axis = transformer.right_judge_x;
            gx = judge_axis; gw = ghost_len;
        }
        DrawTexturedSegmentInternal(renderer_.get(), renderer_->GetNoteHoldTex(),
                                     gx, gy, gw, gh, ghost_color,
                                     is_vertical, cmd.lane == LaneType::Left);
    }

    // ---- 12. 整体边框 ----
    float bx1 = tx, by1 = ty, bx2 = tx + tw, by2 = ty + th;
    if (!hide_ghost && is_active && ghost_len > 2.0f) {
        float jx, jy, jw, jh;
        if (cmd.lane == LaneType::Down) {
            jy = transformer.bottom_judge_y;
            bx1 = std::min(tx, jy);
            by2 = std::max(ty + th, jy + ghost_len);
        } else if (cmd.lane == LaneType::Left) {
            jx = transformer.left_judge_x;
            bx1 = std::min(tx, jx - ghost_len);
            bx2 = std::max(tx + tw, jx);
        } else {
            jx = transformer.right_judge_x;
            bx1 = std::min(tx, jx);
            bx2 = std::max(tx + tw, jx + ghost_len);
        }
    }
    DrawHoldBorder(bx1, by1, bx2 - bx1, by2 - by1,
                   GameplayUI::HOLD_BORDER_THICKNESS,
                   ApplyAlpha(GameplayUI::CLR_NOTE_HOLD_BORDER, cmd.alpha));

    // ---- 13. 进度高亮 ----
    if (is_holding && cmd.hold_progress > 0.0f) {
        uint32_t pc = ApplyAlpha(PackColor(255, 240, 150, 255), cmd.alpha * 0.5f);
        if (cmd.lane == LaneType::Down)
            renderer_->DrawRect(tx, ty + th * (1.0f - cmd.hold_progress), tw, th * cmd.hold_progress, pc);
        else if (cmd.lane == LaneType::Left)
            renderer_->DrawRect(tx + tw * (1.0f - cmd.hold_progress), ty, tw * cmd.hold_progress, th, pc);
        else
            renderer_->DrawRect(tx, ty, tw * cmd.hold_progress, th, pc);
    }

    // ---- 14. head 图标（判定通过后不渲染）----
    if (!hide_head) {
        uint32_t head_color = ApplyAlpha(base_color, cmd.alpha);
        if (is_holding) {
            const Texture* pt = renderer_->GetNoteHoldPressedTex();
            if (pt && pt->valid())
                renderer_->DrawTexture(pt, sr.x, sr.y, sr.w, sr.h, head_color);
            else
                goto head_fallback;
        } else {
head_fallback:
            const Texture* ht = renderer_->GetNoteHoldHeadTex();
            if (ht && ht->valid())
                renderer_->DrawTexture(ht, sr.x, sr.y, sr.w, sr.h, head_color);
            else
                renderer_->DrawRoundedRect(sr.x, sr.y, sr.w, sr.h,
                                           std::min(sr.w, sr.h) * 0.3f, head_color);
        }
    }
}

void HoldNoteRenderer::DrawHoldBorder(float x, float y, float w, float h,
                                       float thickness, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    renderer_->DrawRect(x, y, w, thickness, color);
    renderer_->DrawRect(x, y + h - thickness, w, thickness, color);
    renderer_->DrawRect(x, y, thickness, h, color);
    renderer_->DrawRect(x + w - thickness, y, thickness, h, color);
}
