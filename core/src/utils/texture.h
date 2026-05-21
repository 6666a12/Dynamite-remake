#pragma once
#include <string>
#include <cstdint>

/**
 * OpenGL ES 纹理封装 —— RAII 管理
 * 
 * 安全设计：所有纹理在析构时自动 glDeleteTextures，防止泄漏
 */
class Texture {
public:
    Texture() = default;
    explicit Texture(const std::string& path);
    ~Texture();

    // 禁止拷贝，允许移动
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    bool loadFromFile(const std::string& path);
    bool loadFromMemory(const uint8_t* data, int width, int height, int channels);

    uint32_t id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool valid() const { return id_ != 0; }

    void bind(uint32_t slot = 0) const;

private:
    uint32_t id_ = 0;
    int width_ = 0;
    int height_ = 0;
};
