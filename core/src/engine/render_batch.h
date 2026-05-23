#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include "chart_parser.h"

// 前向声明
class Texture;
struct NoteSkin;

/**
 * 2D 批处理渲染器 —— OpenGL ES 3.0
 * 
 * 设计决策：
 * 1. 每帧收集所有精灵提交，按纹理排序后合批绘制，减少 draw call
 * 2. 使用单个 VBO + IBO，通过 glBufferSubData 更新每帧数据
 * 3. 支持旋转、UV 裁剪、颜色调制
 */
struct SpriteVertex {
    float x, y;       // 位置
    float u, v;       // UV
    uint32_t color;   // RGBA
};

class RenderBatch {
public:
    void init();
    void shutdown();
    void beginFrame(int screen_w, int screen_h);

    // 提交精灵（自动按纹理合批）
    void submit(const Texture* tex,
                float x, float y, float w, float h,
                uint32_t color, float rotation = 0.0f,
                float uv_x = 0.0f, float uv_y = 0.0f,
                float uv_w = 1.0f, float uv_h = 1.0f);

    // Note 专用：根据下落曲线计算位置
    void submitNote(const NoteData& note,
                    int64_t audio_now_ms,
                    float approach_speed,
                    const NoteSkin& skin);

    // 文字渲染（stb_truetype 光栅化后提交为纹理）
    void submitText(const std::string& text,
                    float x, float y,
                    float scale,
                    uint32_t color);

        // 绘制填充矩形（无纹理）
    void submitRect(float x, float y, float w, float h, uint32_t color);

    // 绘制 45° 棋盘格斜纹矩形（方向: 1=向右滚动(底部), -1=向左滚动(顶部)）
    void submitStripedRect(float x, float y, float w, float h,
                           uint32_t base_color, uint32_t stripe_color,
                           int direction, float offset);

    // 绘制圆角矩形（使用 SDF 或分段近似）
    void submitRoundedRect(float x, float y, float w, float h, float radius, uint32_t color);

    void flush(); // 执行 glDrawArrays 或 glDrawElements
    void endFrame();

    // 设置正交投影矩阵
    void setProjection(float left, float right, float bottom, float top);

    // 获取 Note 纹理
    const Texture* getNoteTapTex() const { return note_tap_tex_.get(); }
    const Texture* getNoteHoldTex() const { return note_hold_tex_.get(); }
    const Texture* getNoteHoldHeadTex() const { return note_hold_head_tex_.get(); }
    const Texture* getNoteHoldPressedTex() const { return note_hold_pressed_tex_.get(); }
    const Texture* getNoteHoldTailTex() const { return note_hold_tail_tex_.get(); }
    const Texture* getNoteSlideTex() const { return note_slide_tex_.get(); }
    const Texture* getNoteHoldZeroTex() const { return note_hold_zero_tex_.get(); }
    const Texture* getEffectTex(const std::string& name, int index) const;

    // 获取当前屏幕尺寸
    int screenWidth() const { return screen_w_; }
    int screenHeight() const { return screen_h_; }

private:
    uint32_t vao_ = 0, vbo_ = 0, ibo_ = 0;
    uint32_t shader_program_ = 0;
    int screen_w_ = 0, screen_h_ = 0;

    std::vector<SpriteVertex> vertices_;
    std::vector<uint16_t> indices_;
    const Texture* current_tex_ = nullptr;

    static constexpr size_t MAX_BATCH_VERTICES = 65536;

        uint32_t compileShader(const char* src, uint32_t type);
    uint32_t linkProgram(uint32_t vs, uint32_t fs);
    void flushBatch();

        // 斜纹专用着色器程序和 uniform 位置
    // 注：GLint 在 .cpp 中定义，此处用 int 保持跨平台
    uint32_t stripe_shader_ = 0;
    int stripe_uProjection_ = -1;
    int stripe_uScreenSize_ = -1;
    int stripe_uDirection_ = -1;
    int stripe_uOffset_ = -1;
    int stripe_uStripeColor_ = -1;

    // Note 纹理
    std::unique_ptr<Texture> note_tap_tex_;
    std::unique_ptr<Texture> note_hold_tex_;
    std::unique_ptr<Texture> note_hold_head_tex_;
    std::unique_ptr<Texture> note_hold_pressed_tex_;
    std::unique_ptr<Texture> note_hold_tail_tex_;
    std::unique_ptr<Texture> note_hold_zero_tex_;
    std::unique_ptr<Texture> note_slide_tex_;
    // 判定特效纹理: map<name, vector<Texture>>
    std::vector<std::unique_ptr<Texture>> effect_tap_tex_;
    std::vector<std::unique_ptr<Texture>> effect_hold_tex_;
    std::vector<std::unique_ptr<Texture>> effect_slide_tex_;
    void loadNoteTextures();
};

/**
 * Note 皮肤结构
 */
struct NoteSkin {
    std::string tap_path;
    std::string hold_head_path;
    std::string hold_body_path;
    std::string hold_tail_path;
    std::string slide_path;
};


