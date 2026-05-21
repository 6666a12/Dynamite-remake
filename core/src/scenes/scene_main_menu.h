#pragma once
#include "scene_base.h"
#include <cstdint>
#include <vector>
#include <string>
#include <memory>

class Texture;

/**
 * 主菜单场景（横屏 1920x1080）
 *
 * 布局：
 * ┌──────────────────────────────────────────┐
 * │  头像 用户名  [设置]                      │  ← Top bar (72px)
 * ├──────────────────────────────────────────┤
 * │                                          │
 * │           D Y N A M I T E                │  ← 游戏标题
 * │                                          │
 * │       ┌──────────┐  ┌──────────┐         │
 * │       │  PLAY    │  │  SHOP    │         │  ← 功能按钮
 * │       └──────────┘  └──────────┘         │
 * │       ┌──────────┐  ┌──────────┐         │
 * │       │  EVENT   │  │  SKILL   │         │
 * │       └──────────┘  └──────────┘         │
 * │                                          │
 * ├──────────────────────────────────────────┤
 * │           底部状态 / 版本号               │  ← Footer (48px)
 * └──────────────────────────────────────────┘
 */
class SceneMainMenu : public SceneBase {
public:
    void init() override;
    void enter() override;
    void exit() override;
    void update(int64_t audio_now_ms) override;
    void render(RenderBatch& batch, int64_t audio_now_ms) override;
    void handleInput(const std::vector<RawTouch>& touches, int64_t audio_now_ms) override;

private:
    static constexpr int kDesignW = 1920;
    static constexpr int kDesignH = 1080;
    static constexpr float kTopBarH = 72.0f;
    static constexpr float kFooterH = 48.0f;

    struct MenuButton {
        float x, y, w, h;
        std::string label;
        SceneID target_scene;
    };
    std::vector<MenuButton> buttons_;

    static constexpr float kBtnW = 280.0f;
    static constexpr float kBtnH = 100.0f;
    static constexpr float kBtnGapX = 60.0f;
    static constexpr float kBtnGapY = 40.0f;

    void initButtons();
    void drawTopBar(RenderBatch& batch);
    void drawMenuButtons(RenderBatch& batch);
    void drawFooter(RenderBatch& batch);

    float designScaleX(int screen_w) const { return static_cast<float>(screen_w) / kDesignW; }
    float designScaleY(int screen_h) const { return static_cast<float>(screen_h) / kDesignH; }
};
