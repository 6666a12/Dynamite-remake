#pragma once
#include <memory>

class IRenderer;
class Texture;

/**
 * 背景渲染器
 *
 * 绘制歌曲封面（暗化 + 模糊效果）
 * 无封面时回退到纯色背景
 */
class BackgroundRenderer {
public:
    explicit BackgroundRenderer(std::shared_ptr<IRenderer> renderer);

    void Render(const Texture* cover_tex, float darken,
                float screen_w, float screen_h);

private:
    std::shared_ptr<IRenderer> renderer_;
};
