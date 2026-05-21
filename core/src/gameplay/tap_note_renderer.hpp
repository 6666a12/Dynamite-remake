#pragma once
#include "gameplay_render_command.hpp"
#include "lane_coordinate.hpp"
#include <memory>

class IRenderer;

/**
 * Tap Note 渲染器
 *
 * 外观：
 *   - 下轨道：水平圆角矩形，青色 #38BDF8
 *   - 侧轨道：垂直圆角矩形（因 out_w=t, out_h=w 自然旋转），红色 #FF4444
 *   - 所有轨道均使用同一套圆角矩形绘制，仅通过 LaneCoordinateTransformer 得到不同 AABB
 *
 * 材质支持：优先使用 NoteTapTex 纹理，无纹理时回退到圆角矩形
 */
class TapNoteRenderer {
public:
    explicit TapNoteRenderer(std::shared_ptr<IRenderer> renderer);

    void Render(const NoteRenderCommand& cmd,
                const LaneCoordinateTransformer& transformer);

private:
    std::shared_ptr<IRenderer> renderer_;
};
