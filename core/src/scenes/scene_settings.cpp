/**
 * Settings 场景实现 —— 调整备注流速、偏移、开关
 *
 * 布局：Header(72px) | Content(滑块 + 开关) | Footer(64px)
 */

#include "scenes/scene_settings.h"
#include "engine/render_batch.h"
#include "engine/input_manager.h"
#include "utils/logger.h"
#include "utils/config_manager.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include "../gameplay/gameplay_ui_config.hpp"

// ============================================================
// 生命周期
// ============================================================

void SceneSettings::init() {
    initSliders();
    initToggles();
    syncFromConfig();
}

void SceneSettings::enter() {
    syncFromConfig();
}

void SceneSettings::exit() {
    syncToConfig();
    ConfigManager::instance().save();
}

void SceneSettings::update(int64_t audio_now_ms) {
    stripe_time_ms_ = audio_now_ms;  // 驱动斜纹滚动
    (void)audio_now_ms;
}

// ============================================================
// 数据初始化
// ============================================================

void SceneSettings::initSliders() {
    sliders_ = {
        { SliderKind::NOTE_SPEED,   "NOTE SPEED",   "x",   0.3f,  2.5f, 0.1f, 1.0f },
        { SliderKind::OFFSET_MS,    "OFFSET",        "ms", -500,   500,  5,    0    },
        { SliderKind::AUDIO_BUFFER, "AUDIO BUFFER",  "",    64,    512,  16,   128  },
        { SliderKind::BRIGHTNESS,   "BRIGHTNESS",    "%",   20,    100,  5,    100  },
    };
}

void SceneSettings::initToggles() {
    toggles_ = {
        { ToggleKind::MIRROR,    "MIRROR",     false },
        { ToggleKind::BLEED,     "BLEED",      false },
        { ToggleKind::AUTO_PLAY, "AUTO PLAY",  false },
    };
}

void SceneSettings::syncFromConfig() {
    auto& cfg = ConfigManager::instance();

    for (auto& s : sliders_) {
        switch (s.kind) {
            case SliderKind::NOTE_SPEED:
                s.current_val = cfg.noteSpeed();
                break;
            case SliderKind::OFFSET_MS:
                s.current_val = static_cast<float>(cfg.offsetMs());
                break;
            case SliderKind::AUDIO_BUFFER:
                // 未持久化到 config，使用本地默认值
                break;
            case SliderKind::BRIGHTNESS:
                // 未持久化，使用本地默认值
                break;
        }
    }

    for (auto& t : toggles_) {
        switch (t.kind) {
            case ToggleKind::MIRROR:   t.value = cfg.mirrorMod();  break;
            case ToggleKind::BLEED:    t.value = cfg.bleedMod();   break;
            case ToggleKind::AUTO_PLAY: t.value = cfg.autoPlay();  break;
        }
    }
}

void SceneSettings::syncToConfig() {
    auto& cfg = ConfigManager::instance();

    for (const auto& s : sliders_) {
        switch (s.kind) {
            case SliderKind::NOTE_SPEED:
                cfg.setNoteSpeed(s.current_val);
                break;
            case SliderKind::OFFSET_MS:
                cfg.setOffsetMs(static_cast<int>(s.current_val));
                break;
            default: break;
        }
    }

    for (const auto& t : toggles_) {
        switch (t.kind) {
            case ToggleKind::MIRROR:   cfg.setMirrorMod(t.value);  break;
            case ToggleKind::BLEED:    cfg.setBleedMod(t.value);   break;
            case ToggleKind::AUTO_PLAY: cfg.setAutoPlay(t.value);  break;
        }
    }
}

// ============================================================
// 渲染
// ============================================================

void SceneSettings::render(RenderBatch& batch, int64_t audio_now_ms) {
    (void)audio_now_ms;

    // 全屏背景
    batch.submitRect(0.0f, 0.0f, static_cast<float>(kDesignW), static_cast<float>(kDesignH),
                     PackColor(15, 15, 19, 255));

    drawHeader(batch);
    drawSliders(batch);
    drawToggles(batch);
    drawFooter(batch);
}

// ============================================================
// Header
// ============================================================

void SceneSettings::drawHeader(RenderBatch& batch) {
    float hw = static_cast<float>(kDesignW);

    // Header 背景
    batch.submitRect(0.0f, 0.0f, hw, kHeaderH, PackColor(26, 26, 31, 255));

    // 45° 斜纹（方向=-1: 向左滚动）
    float stripe_offset = static_cast<float>(stripe_time_ms_) * 0.05f;
    batch.submitStripedRect(0.0f, 0.0f, hw, kHeaderH,
                            PackColor(26, 26, 31, 0),
                            PackColor(30, 30, 30, 64),
                            -1, stripe_offset);

    // 返回按钮（左上角）
    batch.submitRoundedRect(20.0f, 14.0f, 80.0f, 44.0f, 22.0f,
                            PackColor(55, 55, 65, 255));
    batch.submitText("BACK", 34.0f, 26.0f, 0.7f,
                     PackColor(255, 255, 255, 255));

    // 标题居中
    batch.submitText("SETTINGS", hw * 0.5f - 70.0f, 22.0f, 1.0f,
                     PackColor(255, 255, 255, 255));
}

// ============================================================
// 滑块渲染
// ============================================================

void SceneSettings::drawSliders(RenderBatch& batch) {
    for (size_t i = 0; i < sliders_.size(); ++i) {
        drawSliderRow(batch, sliders_[i], static_cast<int>(i));
    }
}

void SceneSettings::drawSliderRow(RenderBatch& batch, const SliderState& s, int row_index) {
    float y = kRowStartY + row_index * kRowH;

    // 行背景（隔行交替）
    if (row_index % 2 == 1) {
        batch.submitRect(kRowPadX - 10.0f, y - 4.0f,
                         static_cast<float>(kDesignW) - 2.0f * kRowPadX + 20.0f, kRowH - 8.0f,
                         PackColor(22, 22, 28, 120));
    }

    // 标签（左对齐）
    batch.submitText(s.label, kRowPadX, y + 22.0f, 0.75f,
                     PackColor(226, 232, 240, 255));

    // 滑块轨道背景
    float track_x = kRowPadX + kLabelW;
    float track_y = y + kRowH * 0.5f - kSliderH * 0.5f;
    batch.submitRoundedRect(track_x, track_y, kSliderW, kSliderH, kSliderH * 0.5f,
                            PackColor(55, 55, 65, 255));

    // 滑块已填充部分
    float ratio = (s.current_val - s.min_val) / (s.max_val - s.min_val);

    // 流速非线性修正填充宽度
    if (s.kind == SliderKind::NOTE_SPEED) {
        float v = s.current_val;
        if (v <= 1.0f) {
            ratio = (v - 0.3f) / (1.0f - 0.3f) * 0.5f;
        } else {
            ratio = 0.5f + (v - 1.0f) / (2.5f - 1.0f) * 0.5f;
        }
    }

    float fill_w = kSliderW * ratio;
    if (fill_w > 2.0f) {
        batch.submitRoundedRect(track_x, track_y, fill_w, kSliderH, kSliderH * 0.5f,
                                PackColor(59, 130, 246, 220));
    }

    // 拖拽手柄（圆形）
    float thumb_x = track_x + fill_w;
    float thumb_y = y + kRowH * 0.5f;
    batch.submitRoundedRect(thumb_x - kThumbR, thumb_y - kThumbR,
                            kThumbR * 2.0f, kThumbR * 2.0f, kThumbR,
                            s.is_dragging
                                ? PackColor(255, 255, 255, 255)
                                : PackColor(255, 255, 255, 220));

    // 数值显示（右侧，等宽数字）
    std::ostringstream val_ss;
    val_ss << std::fixed << std::setprecision(1) << s.current_val << s.unit;
    float val_x = track_x + kSliderW + 30.0f;
    batch.submitText(val_ss.str(), val_x, y + 22.0f, 0.8f,
                     PackColor(255, 255, 255, 255), true);
}

// ============================================================
// 开关渲染
// ============================================================

void SceneSettings::drawToggles(RenderBatch& batch) {
    float toggle_start_y = kRowStartY + sliders_.size() * kRowH + kSectionGap;

    // 分区标题
    batch.submitText("PLAY MODS", kRowPadX, toggle_start_y - 8.0f, 0.65f,
                     PackColor(107, 114, 128, 255));

    for (size_t i = 0; i < toggles_.size(); ++i) {
        drawToggleRow(batch, toggles_[i], static_cast<int>(i));
    }
}

void SceneSettings::drawToggleRow(RenderBatch& batch, const ToggleState& t, int row_index) {
    float toggle_start_y = kRowStartY + sliders_.size() * kRowH + kSectionGap;
    float y = toggle_start_y + 20.0f + row_index * kRowH;

    // 行背景
    if (row_index % 2 == 1) {
        batch.submitRect(kRowPadX - 10.0f, y - 4.0f,
                         static_cast<float>(kDesignW) - 2.0f * kRowPadX + 20.0f, kRowH - 8.0f,
                         PackColor(22, 22, 28, 120));
    }

    // 标签
    batch.submitText(t.label, kRowPadX, y + 22.0f, 0.75f,
                     PackColor(226, 232, 240, 255));

    // 开关背景
    float toggle_x = kRowPadX + kLabelW;
    float toggle_y = y + (kRowH - kToggleH) * 0.5f;

    if (t.value) {
        // ON 状态
        batch.submitRoundedRect(toggle_x, toggle_y, kToggleW, kToggleH, kToggleH * 0.5f,
                                PackColor(59, 130, 246, 255));
        batch.submitText("ON", toggle_x + 28.0f, toggle_y + 8.0f, 0.7f,
                         PackColor(255, 255, 255, 255));
    } else {
        // OFF 状态
        batch.submitRoundedRect(toggle_x, toggle_y, kToggleW, kToggleH, kToggleH * 0.5f,
                                PackColor(55, 55, 65, 255));
        batch.submitText("OFF", toggle_x + 22.0f, toggle_y + 8.0f, 0.7f,
                         PackColor(156, 163, 175, 255));
    }
}

// ============================================================
// Footer
// ============================================================

void SceneSettings::drawFooter(RenderBatch& batch) {
    const float ft_y = static_cast<float>(kDesignH) - kFooterH;
    batch.submitRect(0.0f, ft_y,
                     static_cast<float>(kDesignW), kFooterH,
                     PackColor(15, 15, 15, 240));

    // 45° 斜纹
    float stripe_offset = static_cast<float>(stripe_time_ms_) * 0.05f;
    batch.submitStripedRect(0.0f, ft_y, static_cast<float>(kDesignW), kFooterH,
                            PackColor(15, 15, 15, 0),
                            PackColor(30, 30, 30, 64),
                            1, stripe_offset);

    batch.submitText("v0.1.0 - Dynamite Rebuild",
                     static_cast<float>(kDesignW) * 0.5f - 100.0f,
                     ft_y + 14.0f,
                     0.6f, PackColor(107, 114, 128, 255));
}

// ============================================================
// 滑块坐标计算（流速使用非线性映射：中点=1.0, 左=0.3, 右=2.5）
// ============================================================

float SceneSettings::sliderToX(const SliderState& s) const {
    float ratio;
    if (s.kind == SliderKind::NOTE_SPEED) {
        // 非线性映射: 中点=1.0, 左端=0.3, 右端=2.5
        float v = s.current_val;
        if (v <= 1.0f) {
            ratio = (v - 0.3f) / (1.0f - 0.3f) * 0.5f;       // 0.0 ~ 0.5
        } else {
            ratio = 0.5f + (v - 1.0f) / (2.5f - 1.0f) * 0.5f; // 0.5 ~ 1.0
        }
    } else {
        ratio = (s.current_val - s.min_val) / (s.max_val - s.min_val);
    }
    return kRowPadX + kLabelW + kSliderW * ratio;
}

float SceneSettings::xToValue(const SliderState& s, float px) const {
    float track_x = kRowPadX + kLabelW;
    float ratio = (px - track_x) / kSliderW;
    ratio = std::max(0.0f, std::min(1.0f, ratio));

    float val;
    if (s.kind == SliderKind::NOTE_SPEED) {
        // 逆映射: 滑块位置 → 流速值
        if (ratio <= 0.5f) {
            val = 0.3f + ratio / 0.5f * (1.0f - 0.3f);       // 0.3 ~ 1.0
        } else {
            val = 1.0f + (ratio - 0.5f) / 0.5f * (2.5f - 1.0f); // 1.0 ~ 2.5
        }
    } else {
        val = s.min_val + ratio * (s.max_val - s.min_val);
    }

    val = std::round(val / s.step) * s.step;
    return std::max(s.min_val, std::min(s.max_val, val));
}

// ============================================================
// 输入处理
// ============================================================

void SceneSettings::handleInput(const std::vector<RawTouch>& touches,
                                int64_t audio_now_ms) {
    (void)audio_now_ms;

    for (const auto& t : touches) {
        float px = t.x * kDesignW;
        float py = t.y * kDesignH;

        if (t.is_new && t.is_down) {
            // 返回按钮（左上角）
            if (HitTest(px, py, 20.0f, 14.0f, 80.0f, 44.0f)) {
                transition_request_.type = Transition::POP;
                return;
            }

            // 滑块拖动检测：点击手柄或轨道区域
            for (auto& s : sliders_) {
                int row = static_cast<int>(&s - &sliders_[0]);
                float row_y = kRowStartY + row * kRowH;

                if (py >= row_y && py <= row_y + kRowH) {
                    float track_x = kRowPadX + kLabelW;
                    if (HitTest(px, py, track_x - kThumbR, row_y,
                                kSliderW + kThumbR * 2.0f, kRowH)) {
                        s.is_dragging = true;
                        s.current_val = xToValue(s, px);
                        return;
                    }
                }
            }

            // 开关点击检测
            float toggle_start_y = kRowStartY + sliders_.size() * kRowH + kSectionGap + 20.0f;
            for (auto& tg : toggles_) {
                int row = static_cast<int>(&tg - &toggles_[0]);
                float row_y = toggle_start_y + row * kRowH;

                if (py >= row_y && py <= row_y + kRowH) {
                    float toggle_x = kRowPadX + kLabelW;
                    if (HitTest(px, py, toggle_x, row_y, kToggleW, kRowH)) {
                        tg.value = !tg.value;
                        Logger::info("Toggle {} = {}", tg.label, tg.value ? "ON" : "OFF");
                        syncToConfig();
                        return;
                    }
                }
            }

        } else if (t.is_down) {
            // 滑块拖动中
            for (auto& s : sliders_) {
                if (s.is_dragging) {
                    s.current_val = xToValue(s, px);
                    break;
                }
            }

        } else if (!t.is_down) {
            // 手指抬起，停止所有拖动
            for (auto& s : sliders_) {
                if (s.is_dragging) {
                    s.is_dragging = false;
                    syncToConfig();  // 滑块松手时保存
                }
            }
        }
    }
}
