/**
 * 2D 批处理渲染器实现 —— OpenGL ES 3.0
 * 
 * 架构要点：
 * 1. 每帧收集所有精灵的顶点数据，按纹理分组，最后用一次或多次 glDrawElements 绘制
 * 2. 单 VAO + 单 VBO + 单 IBO 管理全部动态顶点数据
 * 3. 使用 1x1 白色纹理作为“无纹理”状态的回退，保证 submitRect 走同一条渲染路径
 * 4. 字体渲染：stb_truetype 预烘焙 ASCII 字符到 atlas 纹理，运行时直接提交 UV 裁切后的四边形
 */

#include "engine/render_batch.h"
#include "utils/texture.h"
#include "utils/logger.h"
#include <cstring>
#include <vector>
#include <cstdio>
#include <cmath>

// 平台相关的 OpenGL ES 3.0 头文件
#if defined(__ANDROID__)
    #include <GLES3/gl3.h>
#elif defined(__APPLE__) && defined(__MACH__)
    #include <OpenGLES/ES3/gl.h>
#else
    // 桌面开发：使用 OpenGL 3.3 Core + glext 暴露 ES 3.0 兼容函数
    #define GL_GLEXT_PROTOTYPES
    #include <GL/gl.h>
    #include <GL/glext.h>
#endif

// glm 用于正交投影矩阵计算
#include <glm/gtc/matrix_transform.hpp>

// stb_truetype 实现：在包含头文件前定义此宏
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

// ============================================================
// 顶点着色器：接收 2D 位置、UV、颜色，应用正交投影矩阵
// ============================================================
static const char* kVertexShaderSrc = R"(
#version 300 es
precision highp float;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform mat4 uProjection;

out vec2 vUV;
out vec4 vColor;

void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";

// ============================================================
// 片段着色器（斜纹版）：根据屏幕坐标生成棋盘格斜纹
// ============================================================
static const char* kStripeFragmentShaderSrc = R"(
#version 300 es
precision highp float;

in vec2 vUV;
in vec4 vColor;

uniform vec2 uScreenSize;
uniform float uDirection;   // 1.0 = 向右(底部), -1.0 = 向左(顶部)
uniform float uOffset;      // 滚动偏移（像素）
uniform vec4 uStripeColor;  // 斜纹颜色（已预乘 alpha）

out vec4 fragColor;

void main() {
    // 从屏幕坐标计算：斜纹沿 (x + y * direction + offset) 方向
    // 使用 gl_FragCoord，这是像素中心坐标（+0.5 偏移）
    float sum = gl_FragCoord.x + gl_FragCoord.y * uDirection + uOffset;

    // 棋盘格：每 4 像素一个周期，2 像素亮 2 像素暗
    float pattern = mod(floor(sum), 4.0);
    float mask = step(pattern, 1.0);  // pattern=0,1 -> mask=1; pattern=2,3 -> mask=0

    // 斜纹透明度渐变（柔和边缘）
    float t = fract(sum);  // 小数部分
    // 在 0~1 和 2~3 范围内边缘渐变
    float smooth_edge = smoothstep(0.0, 1.0, pattern + t);
    
    // 混合：mask=1 时用斜纹色，mask=0 时用底色
    vec4 base = vColor;
    vec4 stripe = uStripeColor;
    fragColor = mix(base, stripe, mask * stripe.a);
}
)";

// ============================================================
// 片段着色器：采样 2D 纹理，与顶点颜色相乘
// ============================================================
static const char* kFragmentShaderSrc = R"(
#version 300 es
precision highp float;

in vec2 vUV;
in vec4 vColor;

uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    vec4 texColor = texture(uTexture, vUV);
    fragColor = texColor * vColor;
}
)";

// ============================================================
// 文件级静态资源（无法加入类头文件时的替代方案）
// ============================================================

/** 1x1 白色纹理 ID，用于纯色矩形绘制（submitRect 的纹理回退） */
static GLuint g_white_tex = 0;

/** GPU 状态缓存：避免重复绑定同一对象 */
static GLuint g_bound_vao = 0;
static GLuint g_bound_program = 0;
static GLuint g_bound_tex_id = 0;

/** 字体图集结构：预渲染 ASCII 32~126 到一张 atlas 纹理 */
struct FontAtlas {
    bool initialized = false;               // 是否已完成初始化
    stbtt_bakedchar cdata[96];              // 96 个可打印 ASCII 字符的 bake 信息
    Texture texture;                        // 纹理对象（RAII 管理）
    static constexpr int kAtlasW = 512;     // atlas 宽度
    static constexpr int kAtlasH = 512;     // atlas 高度
    static constexpr int kFontSize = 32;    // 烘焙字号
};

/** 全局字体图集实例 */
static FontAtlas g_font_atlas;

// ============================================================
// 辅助函数
// ============================================================

/** 将 RGBA 四分量打包为 uint32_t（little-endian 内存布局为 R,G,B,A） */
static inline uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r)
         | (static_cast<uint32_t>(g) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

/** 尝试从常见系统路径加载字体并烘焙 atlas */
static bool InitFontAtlas() {
    if (g_font_atlas.initialized) {
        return true;
    }

    // 按优先级尝试的字体路径列表
    // Android: .ttf 优先，.ttc 需要指定正确的子字体索引
    const char* font_paths[] = {
        "fonts/default.ttf",
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/DroidSansFallbackFull.ttf",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "assets/fonts/default.ttf",
        "assets/fonts/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        nullptr
    };

    FILE* fp = nullptr;
    const char* selected_path = nullptr;
    for (int i = 0; font_paths[i] != nullptr; ++i) {
        fp = fopen(font_paths[i], "rb");
        if (fp) {
            selected_path = font_paths[i];
            break;
        }
    }
    if (!fp) {
        Logger::warn("未找到系统字体，文字渲染将不可用");
        g_font_atlas.initialized = true; // 标记已尝试，避免每帧重试
        return false;
    }

    // 读取字体文件到内存
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> font_buffer(file_size);
    fread(font_buffer.data(), 1, file_size, fp);
    fclose(fp);

    // 申请单通道位图内存供 stbtt_BakeFontBitmap 使用
    std::vector<uint8_t> bitmap(FontAtlas::kAtlasW * FontAtlas::kAtlasH);

    // 对于 .ttc 集合字体，尝试多个索引找到有效的子字体
    int font_offset = 0;
    bool is_ttc = (strstr(selected_path, ".ttc") != nullptr);
    if (is_ttc) {
        for (int idx = 0; idx < 4; ++idx) {
            int off = stbtt_GetFontOffsetForIndex(font_buffer.data(), idx);
            if (off >= 0) {
                font_offset = off;
                break;
            }
        }
    }

    // 烘焙字体：将 96 个 ASCII 字符（32~127）渲染到位图
    int bake_result = stbtt_BakeFontBitmap(
        font_buffer.data(), font_offset,
        static_cast<float>(FontAtlas::kFontSize),
        bitmap.data(),
        FontAtlas::kAtlasW,
        FontAtlas::kAtlasH,
        32, 96,
        g_font_atlas.cdata);

    if (bake_result <= 0) {
        Logger::warn("字体烘焙失败：{}", selected_path);
        g_font_atlas.initialized = true; // 标记已尝试，避免每帧重试
        return false;
    }

    // stbtt 输出为单通道灰度，需扩展为 RGBA 以兼容我们的纹理上传逻辑
    std::vector<uint8_t> rgba(FontAtlas::kAtlasW * FontAtlas::kAtlasH * 4);
    for (int i = 0; i < FontAtlas::kAtlasW * FontAtlas::kAtlasH; ++i) {
        uint8_t alpha = bitmap[i];
        rgba[i * 4 + 0] = 255;   // R
        rgba[i * 4 + 1] = 255;   // G
        rgba[i * 4 + 2] = 255;   // B
        rgba[i * 4 + 3] = alpha; // A
    }

    // 上传到 GPU 纹理
    bool ok = g_font_atlas.texture.loadFromMemory(
        rgba.data(), FontAtlas::kAtlasW, FontAtlas::kAtlasH, 4);
    if (!ok) {
        Logger::warn("字体纹理上传失败");
        g_font_atlas.initialized = true; // 标记已尝试，避免每帧重试
        return false;
    }

    g_font_atlas.initialized = true;
    return true;
}

// ============================================================
// RenderBatch 类方法实现
// ============================================================

uint32_t RenderBatch::compileShader(const char* src, uint32_t type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        Logger::error("着色器编译失败：{}", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

uint32_t RenderBatch::linkProgram(uint32_t vs, uint32_t fs) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        Logger::error("着色器链接失败：{}", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

/**
 * 初始化渲染器：编译着色器、创建 VAO/VBO/IBO、生成白色占位纹理
 */
void RenderBatch::init() {
        // ---------- 编译并链接着色器 ----------
    {
        GLuint vs = compileShader(kVertexShaderSrc, GL_VERTEX_SHADER);
        GLuint fs = compileShader(kFragmentShaderSrc, GL_FRAGMENT_SHADER);
        shader_program_ = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    // 斜纹着色器
    {
        GLuint vs = compileShader(kVertexShaderSrc, GL_VERTEX_SHADER);
        GLuint fs = compileShader(kStripeFragmentShaderSrc, GL_FRAGMENT_SHADER);
        stripe_shader_ = linkProgram(vs, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);

        // 获取 uniform 位置
        glUseProgram(stripe_shader_);
        stripe_uProjection_ = glGetUniformLocation(stripe_shader_, "uProjection");
        stripe_uScreenSize_ = glGetUniformLocation(stripe_shader_, "uScreenSize");
        stripe_uDirection_  = glGetUniformLocation(stripe_shader_, "uDirection");
        stripe_uOffset_     = glGetUniformLocation(stripe_shader_, "uOffset");
        stripe_uStripeColor_ = glGetUniformLocation(stripe_shader_, "uStripeColor");
    }

    // ---------- 创建 VAO / VBO / IBO ----------
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);

    glBindVertexArray(vao_);

    // VBO：预分配足够容纳 MAX_BATCH_VERTICES 个顶点的动态缓冲区
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 MAX_BATCH_VERTICES * sizeof(SpriteVertex),
                 nullptr,
                 GL_DYNAMIC_DRAW);

    // IBO：每个四边形 6 个索引，预分配最大索引数
    const size_t max_indices = (MAX_BATCH_VERTICES / 4) * 6;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 max_indices * sizeof(uint16_t),
                 nullptr,
                 GL_DYNAMIC_DRAW);

    // 顶点属性 0：位置 (vec2 float)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(SpriteVertex),
                          reinterpret_cast<void*>(offsetof(SpriteVertex, x)));

    // 顶点属性 1：UV (vec2 float)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          sizeof(SpriteVertex),
                          reinterpret_cast<void*>(offsetof(SpriteVertex, u)));

    // 顶点属性 2：颜色 (vec4 uint8，归一化到 0.0~1.0)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(SpriteVertex),
                          reinterpret_cast<void*>(offsetof(SpriteVertex, color)));

    glBindVertexArray(0);

    // ---------- 创建 1x1 白色纹理 ----------
    uint8_t white_pixel[4] = {255, 255, 255, 255};
    glGenTextures(1, &g_white_tex);
    glBindTexture(GL_TEXTURE_2D, g_white_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, white_pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ---------- 启用 Alpha 混合 ----------
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ---------- 加载真实 Note 纹理 ----------
    loadNoteTextures();
}

/**
 * 释放所有 GPU 资源
 */
void RenderBatch::shutdown() {
    flushBatch();  // 确保残留的批次被绘制

        if (shader_program_) {
        glDeleteProgram(shader_program_);
        shader_program_ = 0;
    }
    if (stripe_shader_) {
        glDeleteProgram(stripe_shader_);
        stripe_shader_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (ibo_) {
        glDeleteBuffers(1, &ibo_);
        ibo_ = 0;
    }
    if (g_white_tex) {
        glDeleteTextures(1, &g_white_tex);
        g_white_tex = 0;
    }

    // 重置 GPU 状态缓存
    g_bound_vao = 0;
    g_bound_program = 0;
    g_bound_tex_id = 0;
}

/**
 * 帧开始：记录屏幕尺寸，重置批次状态
 */
void RenderBatch::beginFrame(int screen_w, int screen_h) {
    screen_w_ = screen_w;
    screen_h_ = screen_h;
    current_tex_ = nullptr;
    vertices_.clear();
    indices_.clear();

    // 默认投影矩阵：原点左上角，Y 向下，与屏幕像素坐标 1:1 对应
    setProjection(0.0f, static_cast<float>(screen_w),
                  static_cast<float>(screen_h), 0.0f);
}

/**
 * 帧结束：刷新所有残留顶点
 */
void RenderBatch::endFrame() {
    flushBatch();
}

/**
 * 从文件加载真实 Note 纹理和判定特效纹理
 */
void RenderBatch::loadNoteTextures() {
    auto loadTex = [](const char* path) -> std::unique_ptr<Texture> {
        auto tex = std::make_unique<Texture>();
        if (!tex->loadFromFile(path)) {
            Logger::warn("Failed to load texture: %s", path);
        }
        return tex;
    };

    // Note 本体纹理
    note_tap_tex_     = loadTex("skins/default/tap.png");
    note_hold_tex_    = loadTex("skins/default/hold.png");
    note_hold_head_tex_ = loadTex("skins/default/hold_head.png");
    note_hold_pressed_tex_ = loadTex("skins/default/hold_pressed.png");
    note_hold_tail_tex_    = loadTex("skins/default/hold_tail.png");
    note_slide_tex_   = loadTex("skins/default/slide.png");
    note_hold_zero_tex_ = loadTex("skins/default/UI0_Sprites_29.png");

    // 判定特效纹理序列
    for (int i = 0; i < 4; ++i) {
        char buf[64];
        snprintf(buf, sizeof(buf), "skins/default/effect_tap_%d.png", i);
        effect_tap_tex_.push_back(loadTex(buf));
        snprintf(buf, sizeof(buf), "skins/default/effect_hold_%d.png", i);
        effect_hold_tex_.push_back(loadTex(buf));
        snprintf(buf, sizeof(buf), "skins/default/effect_slide_%d.png", i);
        effect_slide_tex_.push_back(loadTex(buf));
    }
}

const Texture* RenderBatch::getEffectTex(const std::string& name, int index) const {
    const std::vector<std::unique_ptr<Texture>>* vec = nullptr;
    if (name == "tap") vec = &effect_tap_tex_;
    else if (name == "hold") vec = &effect_hold_tex_;
    else if (name == "slide") vec = &effect_slide_tex_;
    if (!vec || index < 0 || index >= static_cast<int>(vec->size())) return nullptr;
    return (*vec)[index].get();
}

/**
 * 设置正交投影矩阵（像素坐标系）
 */
void RenderBatch::setProjection(float left, float right, float bottom, float top) {
    if (shader_program_ == 0) return;
    glUseProgram(shader_program_);
    glm::mat4 proj = glm::ortho(left, right, bottom, top, -1.0f, 1.0f);
    GLint loc = glGetUniformLocation(shader_program_, "uProjection");
    if (loc >= 0) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, &proj[0][0]);
    }
}

/**
 * 提交一个精灵四边形到当前批次
 * 
 * 自动按纹理切换：若纹理与当前批次不同，先 flush 旧批次
 * 支持旋转（弧度制）和 UV 裁切
 */
void RenderBatch::submit(const Texture* tex,
                         float x, float y, float w, float h,
                         uint32_t color, float rotation,
                         float uv_x, float uv_y,
                         float uv_w, float uv_h) {
    // 纹理切换触发批次刷新
    if (tex != current_tex_) {
        flushBatch();
        current_tex_ = tex;
    }

    // 顶点数超限保护
    if (vertices_.size() + 4 > MAX_BATCH_VERTICES) {
        flushBatch();
    }

    // 计算旋转后的四个角坐标
    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    float c = std::cos(rotation);
    float s = std::sin(rotation);

    auto rotate = [&](float px, float py) -> std::pair<float, float> {
        float dx = px - cx;
        float dy = py - cy;
        return {
            cx + dx * c - dy * s,
            cy + dx * s + dy * c
        };
    };

    auto [x0, y0] = rotate(x,     y);
    auto [x1, y1] = rotate(x + w, y);
    auto [x2, y2] = rotate(x + w, y + h);
    auto [x3, y3] = rotate(x,     y + h);

    uint16_t base_idx = static_cast<uint16_t>(vertices_.size());

    // 按 CCW 顺序添加 4 个顶点
    vertices_.push_back({x0, y0, uv_x,         uv_y,         color});
    vertices_.push_back({x1, y1, uv_x + uv_w,  uv_y,         color});
    vertices_.push_back({x2, y2, uv_x + uv_w,  uv_y + uv_h,  color});
    vertices_.push_back({x3, y3, uv_x,         uv_y + uv_h,  color});

    // 两个三角形组成四边形：(0,1,2) 和 (0,2,3)
    indices_.push_back(base_idx + 0);
    indices_.push_back(base_idx + 1);
    indices_.push_back(base_idx + 2);
    indices_.push_back(base_idx + 0);
    indices_.push_back(base_idx + 2);
    indices_.push_back(base_idx + 3);
}

/**
 * 提交 Note 精灵
 * 
 * 使用程序化生成的纹理 + 顶点颜色调制来区分 Note 类型
 */
void RenderBatch::submitNote(const NoteData& note,
                             int64_t audio_now_ms,
                             float approach_speed,
                             const NoteSkin& skin) {
    (void)skin;            // 外部 PNG 皮肤系统留待后续扩展
    (void)approach_speed;  // 当前使用固定时间映射

    float dt_ms = static_cast<float>(note.time_ms - audio_now_ms);
    float approach_time = 1500.0f;
    if (dt_ms > approach_time || dt_ms < -300.0f) {
        return;
    }

    float t = 1.0f - (dt_ms / approach_time);

    const float screen_w = static_cast<float>(screen_w_);
    const float screen_h = static_cast<float>(screen_h_);
    const float side_w   = screen_w * 0.25f;
    const float judge_y  = screen_h * 0.75f;
    const float fall_h   = screen_h * 0.65f;

    float nx = 0.0f, ny = 0.0f;
    float nw = 80.0f, nh = 40.0f;

    switch (note.side) {
        case SideType::LEFT:
            nx = side_w * 0.5f - nw * 0.5f;
            ny = (judge_y - fall_h) + fall_h * t;
            break;
        case SideType::DOWN:
            nx = screen_w * 0.5f - nw * 0.5f;
            ny = (judge_y - fall_h) + fall_h * t;
            break;
        case SideType::RIGHT:
            nx = screen_w - side_w * 0.5f - nw * 0.5f;
            ny = (judge_y - fall_h) + fall_h * t;
            break;
    }

    // 根据 Note 类型选择纹理和颜色
    const Texture* tex = nullptr;
    uint32_t color;
    switch (note.type) {
        case NoteType::TAP:
            tex = note_tap_tex_.get();
            color = PackColor(0,   200, 255, 255);
            break;
        case NoteType::HOLD_HEAD:
            tex = note_hold_tex_.get();
            color = PackColor(255, 200, 0,   255);
            break;
        case NoteType::HOLD_BODY:
            tex = note_hold_tex_.get();
            color = PackColor(255, 200, 0,   180);
            break;
        case NoteType::HOLD_TAIL:
            tex = note_hold_tex_.get();
            color = PackColor(255, 200, 0,   255);
            break;
        case NoteType::SLIDE:
            tex = note_slide_tex_.get();
            color = PackColor(200, 0,   255, 255);
            break;
        case NoteType::MULTI:
            tex = note_tap_tex_.get();
            color = PackColor(255, 80,  80,  255);
            break;
        default:
            color = PackColor(255, 255, 255, 255);
            break;
    }

    submit(tex, nx, ny, nw, nh, color);
}

/**
 * 文字渲染：使用 stb_truetype 预烘焙的字体 atlas
 * 
 * 每个字符独立提交为一个 UV 裁切的精灵。
 * 若字体未加载，函数静默返回（不影响其他渲染）。
 */
void RenderBatch::submitText(const std::string& text,
                             float x, float y,
                             float scale,
                             uint32_t color) {
    if (!g_font_atlas.initialized) {
        if (!InitFontAtlas()) {
            return;  // 字体加载失败，放弃文字渲染
        }
    }
    if (!g_font_atlas.texture.valid()) {
        return;
    }

    // stbtt_GetBakedQuad 会修改 xpos/ypos 作为光标前进位置
    // 我们在“字体像素坐标空间”计算，最后将输出坐标整体乘以 scale
    float xpos = 0.0f;
    float ypos = 0.0f;

    for (char ch : text) {
        if (ch < 32 || ch >= 128) {
            continue;  // 跳过不可打印字符
        }

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(g_font_atlas.cdata,
                           FontAtlas::kAtlasW,
                           FontAtlas::kAtlasH,
                           ch - 32,
                           &xpos, &ypos,
                           &q, 1);  // 1 = OpenGL 坐标系（Y 向下）

        // 将像素坐标平移到 (x,y) 锚点并缩放
        float dx = x + q.x0 * scale;
        float dy = y + q.y0 * scale;
        float dw = (q.x1 - q.x0) * scale;
        float dh = (q.y1 - q.y0) * scale;

        submit(&g_font_atlas.texture,
               dx, dy, dw, dh,
               color, 0.0f,
               q.s0, q.t0,
               q.s1 - q.s0, q.t1 - q.t0);
    }
}

/**
 * 绘制纯色填充矩形（内部使用白色纹理 + 顶点颜色调制）
 */
void RenderBatch::submitRect(float x, float y, float w, float h, uint32_t color) {
    // 传入 nullptr 作为纹理，flushBatch 会自动回退到白色纹理
    submit(nullptr, x, y, w, h, color);
}

/**
 * 绘制圆角矩形（简化实现：用多个小矩形分段近似四个圆角）
 * 
 * 说明：精确圆角需要更多顶点和 SDF 着色器，此处用 5 个矩形拼接实现视觉近似：
 * - 中心大矩形
 * - 上下两条窄矩形（覆盖圆角垂直部分）
 * - 左右两条窄矩形（覆盖圆角水平部分）
 */
void RenderBatch::submitRoundedRect(float x, float y, float w, float h,
                                    float radius, uint32_t color) {
    // 限制圆角半径不超过短边的一半
    float r = std::min(radius, std::min(w, h) * 0.5f);

    // 中心主体矩形
    submitRect(x + r, y, w - 2.0f * r, h, color);
    // 左侧填充条
    submitRect(x, y + r, r, h - 2.0f * r, color);
    // 右侧填充条
    submitRect(x + w - r, y + r, r, h - 2.0f * r, color);
    // 上侧填充条（覆盖左右上角之间的区域）
    submitRect(x + r, y, w - 2.0f * r, r, color);
    // 下侧填充条
    submitRect(x + r, y + h - r, w - 2.0f * r, r, color);

    // 四个角用小矩形近似（非完美圆角，但在移动端足够快）
    // 左上角
    submitRect(x, y, r, r, color);
    // 右上角
    submitRect(x + w - r, y, r, r, color);
    // 左下角
    submitRect(x, y + h - r, r, r, color);
    // 右下角
    submitRect(x + w - r, y + h - r, r, r, color);
}

/**
 * 绘制 45° 棋盘格斜纹矩形
 * 
 * 使用独立的斜纹着色器，直接通过 gl_FragCoord 计算棋盘格。
 * 不依赖纹理，base_color 作为底色，stripe_color 作为斜纹色。
 * 
 * 方向机制：
 *   direction=1: 斜纹向右滚动（底部栏），公式: x + y + offset
 *   direction=-1: 斜纹向左滚动（顶部栏），公式: x - y + offset
 *   内部实现为: gl_FragCoord.x + gl_FragCoord.y * direction + offset
 */
void RenderBatch::submitStripedRect(float x, float y, float w, float h,
                                     uint32_t base_color, uint32_t stripe_color,
                                     int direction, float offset) {
    // 先刷新普通批次
    flushBatch();

    // 暂存当前着色器以备恢复
    GLuint prev_program = g_bound_program;

    // 切换到斜纹着色器
    if (g_bound_program != stripe_shader_) {
        glUseProgram(stripe_shader_);
        g_bound_program = stripe_shader_;
    }

    // 设置 uniform：投影矩阵、屏幕尺寸、方向、偏移、斜纹颜色
    glm::mat4 proj = glm::ortho(0.0f, static_cast<float>(screen_w_),
                                static_cast<float>(screen_h_), 0.0f, -1.0f, 1.0f);
    if (stripe_uProjection_ >= 0)
        glUniformMatrix4fv(stripe_uProjection_, 1, GL_FALSE, &proj[0][0]);
    if (stripe_uScreenSize_ >= 0)
        glUniform2f(stripe_uScreenSize_, static_cast<float>(screen_w_), static_cast<float>(screen_h_));
    if (stripe_uDirection_ >= 0)
        glUniform1f(stripe_uDirection_, static_cast<float>(direction));
    if (stripe_uOffset_ >= 0)
        glUniform1f(stripe_uOffset_, offset);
    if (stripe_uStripeColor_ >= 0) {
        float a = static_cast<float>((stripe_color >> 24) & 0xFF) / 255.0f;
        float r = static_cast<float>((stripe_color >> 0) & 0xFF) / 255.0f;
        float g = static_cast<float>((stripe_color >> 8) & 0xFF) / 255.0f;
        float b = static_cast<float>((stripe_color >> 16) & 0xFF) / 255.0f;
        glUniform4f(stripe_uStripeColor_, r, g, b, a);
    }

    // ---- 手动构建顶点，不走 submit() 以避免着色器冲突 ----
    // 确保有空间
    if (vertices_.size() + 4 > MAX_BATCH_VERTICES) {
        // 理论上不会发生，因刚 flush
    }

    uint16_t base_idx = static_cast<uint16_t>(vertices_.size());
    vertices_.push_back({x,     y,     0.0f, 0.0f, base_color});
    vertices_.push_back({x + w, y,     1.0f, 0.0f, base_color});
    vertices_.push_back({x + w, y + h, 1.0f, 1.0f, base_color});
    vertices_.push_back({x,     y + h, 0.0f, 1.0f, base_color});

    indices_.push_back(base_idx + 0);
    indices_.push_back(base_idx + 1);
    indices_.push_back(base_idx + 2);
    indices_.push_back(base_idx + 0);
    indices_.push_back(base_idx + 2);
    indices_.push_back(base_idx + 3);

    current_tex_ = nullptr; // 确保 flushBatch 使用白色纹理

    // ---- 手动 flush（使用当前绑定的 stripe_shader_） ----
    if (vertices_.empty() || indices_.empty()) {
        return;
    }

    // VAO 缓存
    if (g_bound_vao != vao_) {
        glBindVertexArray(vao_);
        g_bound_vao = vao_;
    }

    // 上传顶点数据
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    vertices_.size() * sizeof(SpriteVertex),
                    vertices_.data());

    // 上传索引数据
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                    indices_.size() * sizeof(uint16_t),
                    indices_.data());

    // 纹理绑定（回退到白色纹理）
    GLuint desired_tex = (current_tex_ != nullptr && current_tex_->valid())
                         ? current_tex_->id()
                         : g_white_tex;
    if (g_bound_tex_id != desired_tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, desired_tex);
        g_bound_tex_id = desired_tex;
    }

    // 绘制调用（此时 g_bound_program == stripe_shader_，正确）
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(indices_.size()),
                   GL_UNSIGNED_SHORT,
                   nullptr);

    vertices_.clear();
    indices_.clear();

    // ---- 恢复普通着色器 ----
    if (prev_program != stripe_shader_) {
        glUseProgram(prev_program ? prev_program : shader_program_);
        g_bound_program = prev_program ? prev_program : shader_program_;
    }
    current_tex_ = nullptr;
}

/**
 * 外部触发刷新（绘制当前累积的所有顶点）
 */
void RenderBatch::flush() {
    flushBatch();
}

/**
 * 内部刷新：上传顶点/索引数据到 GPU 并执行绘制命令
 */
void RenderBatch::flushBatch() {
    if (vertices_.empty() || indices_.empty()) {
        return;
    }

    // VAO 缓存
    if (g_bound_vao != vao_) {
        glBindVertexArray(vao_);
        g_bound_vao = vao_;
    }

    // 上传顶点数据
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    vertices_.size() * sizeof(SpriteVertex),
                    vertices_.data());

    // 上传索引数据
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                    indices_.size() * sizeof(uint16_t),
                    indices_.data());

    // 着色器缓存
    if (g_bound_program != shader_program_) {
        glUseProgram(shader_program_);
        g_bound_program = shader_program_;
    }

    // 纹理缓存
    GLuint desired_tex = (current_tex_ != nullptr && current_tex_->valid())
                         ? current_tex_->id()
                         : g_white_tex;
    if (g_bound_tex_id != desired_tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, desired_tex);
        g_bound_tex_id = desired_tex;
    }

    // 绘制调用
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(indices_.size()),
                   GL_UNSIGNED_SHORT,
                   nullptr);

    // 清空批次，准备下一组顶点
    vertices_.clear();
    indices_.clear();
}
