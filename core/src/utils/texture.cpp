/**
 * Texture 类实现 —— OpenGL ES 纹理的 RAII 封装
 * 
 * 核心设计：
 * 1. 在 .cpp 中定义 STB_IMAGE_IMPLEMENTATION，使 stb_image 的函数体仅在此编译单元生成
 * 2. 移动语义确保纹理所有权可安全转移，禁止拷贝防止重复释放
 * 3. 析构时自动调用 glDeleteTextures，杜绝泄漏
 */

// 必须在包含 stb_image.h 之前定义此宏，否则只有声明没有实现
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "utils/texture.h"
#include <cstdio>
#include <utility>

// 平台相关的 OpenGL ES 3.0 头文件
#if defined(__ANDROID__)
    #include <GLES3/gl3.h>
#elif defined(__APPLE__) && defined(__MACH__)
    #include <OpenGLES/ES3/gl.h>
#else
    // 桌面开发回退：SDL 提供的 OpenGL 封装
    #define GL_GLEXT_PROTOTYPES
    #include <SDL3/SDL_opengl.h>
#endif

/**
 * 从文件路径构造纹理，加载失败时 id_ 保持为 0
 */
Texture::Texture(const std::string& path) {
    loadFromFile(path);
}

/**
 * 析构函数 —— RAII 核心：自动释放 GPU 纹理对象
 */
Texture::~Texture() {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;  // 置零防止野指针式引用
    }
}

/**
 * 移动构造函数 —— 窃取源对象的 GPU 资源，将源对象置空
 */
Texture::Texture(Texture&& other) noexcept
    : id_(other.id_)
    , width_(other.width_)
    , height_(other.height_) {
    other.id_ = 0;
    other.width_ = 0;
    other.height_ = 0;
}

/**
 * 移动赋值运算符 —— 先释放自身资源，再窃取源对象资源
 */
Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        // 释放当前持有的旧纹理
        if (id_ != 0) {
            glDeleteTextures(1, &id_);
        }
        // 转移所有权
        id_ = other.id_;
        width_ = other.width_;
        height_ = other.height_;
        // 将源对象置空，避免双重释放
        other.id_ = 0;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

/**
 * 从磁盘文件加载 PNG/JPG 纹理
 * 
 * 流程：
 * 1. stbi_load 解码图像为 RGBA 字节数组
 * 2. glGenTextures 创建 GPU 纹理对象
 * 3. glTexImage2D 上传像素数据到显存
 * 4. 设置纹理参数：双线性过滤 + 边缘钳制（避免透明边缘出现杂色）
 */
bool Texture::loadFromFile(const std::string& path) {
    // 若已持有旧纹理，先释放
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }

    int channels = 0;
    // 强制要求 4 通道（RGBA），简化后续上传逻辑
    unsigned char* data = stbi_load(path.c_str(), &width_, &height_, &channels, 4);
    if (!data) {
        // stbi_failure_reason 可获取具体失败原因，此处暂不输出日志
        return false;
    }

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);

    // 纹理过滤：双线性，适合 UI 和 2D 渲染
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // 边缘钳制：UV 超出 0~1 时重复边缘像素，防止透明区域采样到对面像素
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 上传纹理数据：内部格式 RGBA，输入格式 RGBA，数据类型 unsigned byte
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    // 释放 CPU 端解码后的像素内存
    stbi_image_free(data);
    // 解绑，避免影响后续意外操作
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

/**
 * 从内存缓冲区创建纹理
 * 
 * 用途：
 * - 运行时动态生成纹理（如字体 atlas、离屏渲染结果）
 * - 从压缩包或网络资源直接加载，避免写临时文件
 */
bool Texture::loadFromMemory(const uint8_t* data, int width, int height, int channels) {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }

    width_ = width;
    height_ = height;

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 根据通道数选择格式：4 通道用 RGBA，否则用 RGB
    GLenum fmt = (channels == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, width, height, 0,
                 fmt, GL_UNSIGNED_BYTE, data);

    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

/**
 * 将纹理绑定到指定纹理单元
 * 
 * @param slot 纹理单元索引，0 对应 GL_TEXTURE0
 */
void Texture::bind(uint32_t slot) const {
    if (id_ != 0) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, id_);
    }
}
