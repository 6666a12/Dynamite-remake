#pragma once
#include "gameplay_render_command.hpp"
#include "lane_coordinate.hpp"
#include <memory>

class IRenderer;

/**
 * Hold Note 渲染器 —— v1.1 规范 + 真身体/假身体设计
 *
 * 真身体 + 假身体设计（Dynamite 标准）：
 *
 *   [tail]      ← 在远端，后落
 *     |
 *   真身体      ← 正常颜色 (tail→判定线)，过线后从尾端逐渐收起
 *     |
 *   判定线
 *     |
 *   假身体      ← 半透明橙色 (判定线→head)，跟随 head 下落，长度可变
 *     |
 *   [head]      ← 在近端，先落
 *
 * 渲染顺序：
 *   1. 真身体（从 tail 到判定线，正常颜色 + 纹理平铺，过线后收起）
 *   2. 假身体（从判定线到 head，半透明 + 纹理平铺）
 *   3. 边框（整个真身体 + 假身体区域）
 *   4. 进度高亮（按住时从判定线端填充）
 *   5. Hold 头部 + 尾部图标
 */

class HoldNoteRenderer {
public:
    explicit HoldNoteRenderer(std::shared_ptr<IRenderer> renderer);

    void Render(const NoteRenderCommand& cmd,
                const LaneCoordinateTransformer& transformer);

private:
    std::shared_ptr<IRenderer> renderer_;

    // 辅助：根据帧计算假身体/真身体长度
    // 返回 {ghost_len, real_len}，单位：屏幕像素
    // judge_axis = 判定线沿轨道方向的坐标（对 DOWN 是 y，对 LEFT/RIGHT 是 x）
    // head_axis = head 沿轨道方向的外缘坐标
    // tail_axis = tail 沿轨道方向的外缘坐标（超出判定线方向）
    // direction = +1（向远延伸）或 -1（向近延伸）
    struct GhostRealLengths {
        float ghost_len;   // 假身体长度（head→判定线），>0
        float real_len;    // 真身体长度（判定线→tail），>0
    };
    GhostRealLengths CalcGhostRealLengths(
        float head_axis, float tail_axis, float judge_axis,
        float distance,  // note.距离（当前下落进度）
        float full_len,  // 总屏幕长度
        float shrink_progress // 收起进度 0.0~1.0（>1 表示完全收起）
    ) const;

    // 绘制一段带纹理平铺的身体
    void DrawTexturedSegment(const Texture* tex,
                             float x, float y, float w, float h,
                             uint32_t color,
                             bool is_vertical,     // true=DOWN垂直方向, false=LEFT/RIGHT水平方向
                             bool reverse_uv);     // true=UV方向反转

    void DrawHoldBorder(float x, float y, float w, float h,
                        float thickness, uint32_t color);
    void DrawHoldProgress(float x, float y, float w, float h,
                          float progress, uint32_t progress_color,
                          LaneType lane);
};
