#pragma once
#include "gameplay_render_command.hpp"
#include "lane_coordinate.hpp"
#include <memory>

class IRenderer;

/**
 * 判定特效渲染器
 *
 * 特效坐标：通过 transformer.TransformEffect() 将 (lane_pos, distance=0)
 * 映射到屏幕坐标。由于 distance=0，特效始终出现在判定线位置。
 *
 * 特效类型：
 *   - Perfect: 判定线处爆发白色/青色光晕 + 向外扩散粒子，300ms
 *   - Good: 较小范围蓝色光晕，300ms
 *   - Miss: 红色淡出，300ms
 */
class HitEffectRenderer {
public:
    explicit HitEffectRenderer(std::shared_ptr<IRenderer> renderer);

    void Render(const HitEffectCommand& cmd,
                const LaneCoordinateTransformer& transformer);

private:
    std::shared_ptr<IRenderer> renderer_;
};
