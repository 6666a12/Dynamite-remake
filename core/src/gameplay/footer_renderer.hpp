#pragma once
#include <cstdint>
#include <string>
#include <memory>

class IRenderer;

/**
 * 底部信息栏渲染器
 *
 * 显示曲名、难度标签等信息
 */
class FooterRenderer {
public:
    explicit FooterRenderer(std::shared_ptr<IRenderer> renderer);

    void Render(const std::string& song_title,
                const std::string& difficulty_label,
                uint32_t difficulty_color,
                float screen_w, float screen_h);

private:
    std::shared_ptr<IRenderer> renderer_;
};
