/**
 * 主菜单场景 —— 功能入口页（横屏 1920x1080）
 *
 * 功能按钮：PLAY → 选歌，SHOP → 商店，EVENT → 活动，SKILL → 段位
 */

#include "scenes/scene_main_menu.h"
#include "engine/render_batch.h"
#include "engine/input_manager.h"
#include "utils/logger.h"
#include <cmath>
#include "../gameplay/gameplay_ui_config.hpp"



// ============================================================
// 生命周期
// ============================================================

void SceneMainMenu::init() {
    initButtons();
}

void SceneMainMenu::enter() {
    // 每次进入重置状态
}

void SceneMainMenu::exit() {
}

void SceneMainMenu::update(int64_t audio_now_ms) {
    stripe_time_ms_ = audio_now_ms;  // 驱动斜纹滚动
}

// ============================================================
// 按钮布局
// ============================================================

void SceneMainMenu::initButtons() {
    buttons_.clear();

    float center_x = kDesignW * 0.5f;
    float center_y = kDesignH * 0.5f;

    // 2x2 网格
    struct BtnDef { const char* label; SceneID target; };
    BtnDef defs[4] = {
        {"PLAY",  SceneID::SONG_SELECT},
        {"SHOP",  SceneID::SHOP},
        {"EVENT", SceneID::EVENT},
        {"SKILL", SceneID::SKILL_SET},
    };

    for (int i = 0; i < 4; ++i) {
        int col = i % 2;
        int row = i / 2;
        float x = center_x + (col == 0 ? -(kBtnW + kBtnGapX) * 0.5f : (kBtnW + kBtnGapX) * 0.5f) - kBtnW * 0.5f;
        float y = center_y + (row == 0 ? -(kBtnH + kBtnGapY) * 0.5f : (kBtnH + kBtnGapY) * 0.5f) - kBtnH * 0.5f;
        buttons_.push_back({x, y, kBtnW, kBtnH, defs[i].label, defs[i].target});
    }
}

// ============================================================
// 渲染
// ============================================================

void SceneMainMenu::render(RenderBatch& batch, int64_t audio_now_ms) {
    (void)audio_now_ms;

    // 全屏背景
    batch.submitRect(0.0f, 0.0f, static_cast<float>(kDesignW), static_cast<float>(kDesignH),
                     PackColor(15, 15, 15, 255));

    drawTopBar(batch);
    drawMenuButtons(batch);
    drawFooter(batch);
}

void SceneMainMenu::drawTopBar(RenderBatch& batch) {
    // 顶部栏背景
    batch.submitRect(0.0f, 0.0f, static_cast<float>(kDesignW), kHeaderH,
                     PackColor(26, 26, 31, 255));

    // 用户名（左对齐）
    batch.submitText("GUEST", 20.0f, 20.0f, 0.9f,
                     PackColor(255, 255, 255, 255));

    // 设置按钮（右上）
    batch.submitText("SETTINGS", static_cast<float>(kDesignW) - 140.0f, 22.0f, 0.8f,
                     PackColor(156, 163, 175, 255));
}

void SceneMainMenu::drawMenuButtons(RenderBatch& batch) {
    const uint32_t btn_bg = PackColor(36, 36, 43, 255);
    const uint32_t btn_border = PackColor(59, 130, 246, 180);
    const uint32_t text_color = PackColor(255, 255, 255, 255);

    for (const auto& btn : buttons_) {
        // 圆角矩形按钮背景
        batch.submitRoundedRect(btn.x, btn.y, btn.w, btn.h, 16.0f, btn_bg);
        // 上边框强调线
        batch.submitRect(btn.x + 8.0f, btn.y, btn.w - 16.0f, 2.0f, btn_border);
        // 按钮文字
        batch.submitText(btn.label, btn.x + btn.w * 0.5f - 40.0f, btn.y + btn.h * 0.5f - 12.0f,
                         1.2f, text_color);
    }
}

void SceneMainMenu::drawFooter(RenderBatch& batch) {
    const float ft_y = static_cast<float>(kDesignH) - kFooterH;
    batch.submitRect(0.0f, ft_y,
                     static_cast<float>(kDesignW), kFooterH,
                     PackColor(15, 15, 15, 240));
    // 45° 斜纹覆盖 (方向=1: 向右滚动)
    float stripe_offset = static_cast<float>(stripe_time_ms_) * 0.05f;
    batch.submitStripedRect(0.0f, ft_y, static_cast<float>(kDesignW), kFooterH,
                            PackColor(15, 15, 15, 0),
                            PackColor(30, 30, 30, 64),
                            1, stripe_offset);

    batch.submitText("v0.1.0 - Dynamite Rebuild",
                     static_cast<float>(kDesignW) * 0.5f - 100.0f,
                     static_cast<float>(kDesignH) - kFooterH + 14.0f,
                     0.6f, PackColor(107, 114, 128, 255));
}

// ============================================================
// 输入处理
// ============================================================

void SceneMainMenu::handleInput(const std::vector<RawTouch>& touches,
                                int64_t audio_now_ms) {
    (void)audio_now_ms;

    for (const auto& t : touches) {
        if (!t.is_new || !t.is_down) continue;

        float px = t.x * kDesignW;
        float py = t.y * kDesignH;

        // 主菜单按钮（HitTest 自动膨胀到 ≥44dp）
        for (const auto& btn : buttons_) {
            if (HitTest(px, py, btn.x, btn.y, btn.w, btn.h)) {
                transition_request_.type = Transition::PUSH;
                transition_request_.target_scene_id = static_cast<int>(btn.target_scene);
                return;
            }
        }

        // 右上角 SETTINGS 按钮（渲染在 kDesignW-140, 22, scale 0.8）
        if (HitTest(px, py, static_cast<float>(kDesignW) - 144.0f, 0.0f,
                    144.0f, kHeaderH)) {
            transition_request_.type = Transition::PUSH;
            transition_request_.target_scene_id = static_cast<int>(SceneID::SETTINGS);
            return;
        }
    }
}
