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
};

