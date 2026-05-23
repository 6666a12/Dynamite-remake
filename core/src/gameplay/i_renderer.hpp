#pragma once
#include <cstdint>
#include <string>
#include <memory>

class RenderBatch;
class Texture;

/**
 * IRenderer —— 渲染抽象层
 *
 * 封装 RenderBatch 的基本绘图操作，供各个 Note/HUD 渲染器使用。
 * 这样渲染器无需直接依赖 RenderBatch 的全部接口，便于测试和材质切换。
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // ---- 视口变换（等比例缩放 + 居中偏移） ----
    // 所有坐标基于 1920x1080 设计分辨率，
    // SetViewportTransform 在物理屏幕尺寸变化时调用，
    // 自动将逻辑坐标映射到实际屏幕。
    // 归一化公式：
    //   scale = min(screen_w / design_w, screen_h / design_h)
    //   offset_x = (screen_w - design_w * scale) / 2
    //   offset_y = (screen_h - design_h * scale) / 2
    virtual void SetViewportTransform(float scale, float offset_x, float offset_y) = 0;

    // ---- 基础图形 ----
    virtual void DrawRect(float x, float y, float w, float h, uint32_t color) = 0;
    virtual void DrawRoundedRect(float x, float y, float w, float h,
                                 float radius, uint32_t color) = 0;

    // ---- 纹理 ----
    virtual void DrawTexture(const Texture* tex, float x, float y,
                             float w, float h, uint32_t color,
                             float uv_x = 0.0f, float uv_y = 0.0f,
                             float uv_w = 1.0f, float uv_h = 1.0f) = 0;

    // ---- 文字 ----
    virtual void DrawText(const std::string& text, float x, float y,
                          float scale, uint32_t color) = 0;

    // ---- 获取纹理引用 ----
    virtual const Texture* GetNoteTapTex() const = 0;
    virtual const Texture* GetNoteHoldTex() const = 0;
    virtual const Texture* GetNoteHoldHeadTex() const = 0;
    virtual const Texture* GetNoteHoldPressedTex() const = 0;
    virtual const Texture* GetNoteHoldTailTex() const = 0;
    virtual const Texture* GetNoteHoldZeroTex() const = 0;
    virtual const Texture* GetNoteSlideTex() const = 0;
    virtual const Texture* GetEffectTex(const std::string& name, int index) const = 0;
};

/**
 * RenderBatchRenderer —— 基于 RenderBatch 的 IRenderer 实现
 */
class RenderBatchRenderer : public IRenderer {
public:
    explicit RenderBatchRenderer(RenderBatch& batch)
        : batch_(batch) {}

    void SetViewportTransform(float scale, float offset_x, float offset_y) override {
        scale_ = scale;
        offset_x_ = offset_x;
        offset_y_ = offset_y;
    }

    void DrawRect(float x, float y, float w, float h, uint32_t color) override;
    void DrawRoundedRect(float x, float y, float w, float h,
                         float radius, uint32_t color) override;
    void DrawTexture(const Texture* tex, float x, float y,
                     float w, float h, uint32_t color,
                     float uv_x, float uv_y,
                     float uv_w, float uv_h) override;
    void DrawText(const std::string& text, float x, float y,
                  float scale, uint32_t color) override;

    const Texture* GetNoteTapTex() const override;
    const Texture* GetNoteHoldTex() const override;
    const Texture* GetNoteHoldHeadTex() const override;
    const Texture* GetNoteHoldPressedTex() const override;
    const Texture* GetNoteHoldTailTex() const override;
    const Texture* GetNoteHoldZeroTex() const override;
    const Texture* GetNoteSlideTex() const override;
private:
    RenderBatch& batch_;
    float scale_ = 1.0f;
    float offset_x_ = 0.0f;
    float offset_y_ = 0.0f;
};

