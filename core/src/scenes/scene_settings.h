#pragma once
#include "scene_base.h"
#include <cstdint>
#include <vector>
#include <string>

/**
 * Settings 场景 —— 调整备注流速、偏移、开关
 *
 * 布局（横屏 1920x1080）：
 * ┌──────────────────────────────────────────┐
 * │  [BACK]          SETTINGS                │  ← Header (72px)
 * ├──────────────────────────────────────────┤
 * │                                          │
 * │  NOTE SPEED      [========●====]  1.0x   │  ← 非线性滑块
 * │  OFFSET          [====●========]  0ms    │  ← -500 ~ +500
 * │  AUDIO BUFFER    [=====●=======]  128    │
 * │  BRIGHTNESS      [============●]  100%   │
 * │                                          │
 * │  PLAY MODS  ───────────────────          │
 * │  MIRROR          [  ON / OFF  ]          │  ← 开关
 * │  BLEED           [  ON / OFF  ]          │
 * │  AUTO PLAY       [  ON / OFF  ]          │
 * │                                          │
 * ├──────────────────────────────────────────┤
 * │         v0.1.0 - Dynamite Rebuild        │  ← Footer (64px)
 * └──────────────────────────────────────────┘
 */
class SceneSettings : public SceneBase {
public:
    void init() override;
    void enter() override;
    void exit() override;
    void update(int64_t audio_now_ms) override;
    void render(RenderBatch& batch, int64_t audio_now_ms) override;
    void handleInput(const std::vector<RawTouch>& touches, int64_t audio_now_ms) override;

private:
    // ====== 布局常量 ======
    static constexpr float kRowH = 80.0f;          // 每行高度
    static constexpr float kRowPadX = 60.0f;       // 行左右边距
    static constexpr float kRowStartY = 100.0f;    // 第一个设置行 Y 坐标
    static constexpr float kLabelW = 260.0f;       // 标签宽度
    static constexpr float kSliderW = 600.0f;      // 滑块轨道宽度
    static constexpr float kSliderH = 12.0f;       // 滑块轨道高度
    static constexpr float kThumbR = 16.0f;        // 圆形拖拽手柄半径
    static constexpr float kValueW = 100.0f;       // 数值显示宽度
    static constexpr float kToggleW = 90.0f;       // 开关宽度
    static constexpr float kToggleH = 36.0f;       // 开关高度
    static constexpr float kSectionGap = 20.0f;    // 分区间距

    // ====== 滑块数据 ======
    enum class SliderKind {
        NOTE_SPEED,
        OFFSET_MS,
        AUDIO_BUFFER,
        BRIGHTNESS
    };

    struct SliderState {
        SliderKind kind;
        const char* label;
        const char* unit;
        float min_val;
        float max_val;
        float step;
        float current_val;
        bool is_dragging = false;
    };

    std::vector<SliderState> sliders_;

    // ====== 开关数据 ======
    enum class ToggleKind {
        MIRROR,
        BLEED,
        AUTO_PLAY
    };

    struct ToggleState {
        ToggleKind kind;
        const char* label;
        bool value;
    };

    std::vector<ToggleState> toggles_;

    // ====== 内部方法 ======
    void initSliders();
    void initToggles();
    void syncFromConfig();
    void syncToConfig();

    void drawHeader(RenderBatch& batch);
    void drawSliders(RenderBatch& batch);
    void drawSliderRow(RenderBatch& batch, const SliderState& s, int row_index);
    void drawToggles(RenderBatch& batch);
    void drawToggleRow(RenderBatch& batch, const ToggleState& t, int row_index);
    void drawFooter(RenderBatch& batch);

    float sliderToX(const SliderState& s) const;
    float xToValue(const SliderState& s, float x) const;
};
