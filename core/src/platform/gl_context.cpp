/**
 * Dynamite 重构项目 —— OpenGL ES 上下文辅助函数
 *
 * 功能：
 * - 检查并打印 GL 错误
 * - 查询并打印 GPU 信息
 *
 * 使用场景：
 * - 开发调试阶段快速定位 GL 调用错误
 * - 运行时收集 GPU 信息用于兼容性判断
 */

#if defined(__ANDROID__) || defined(__APPLE__)
#include <SDL3/SDL_opengles2.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <cstdio>
#include <string>

// -----------------------------------------------------------------------------
// GL 错误检查
// -----------------------------------------------------------------------------

/**
 * 检查 OpenGL ES 错误状态
 *
 * 遍历 glGetError() 直到无错误，将错误码转换为可读字符串
 *
 * @param file 调用源文件名（通常传 __FILE__）
 * @param line 调用行号（通常传 __LINE__）
 * @param func 调用函数名（通常传 __func__）
 *
 * 使用宏封装：
 *   CHECK_GL_ERROR();
 */
void CheckGLErrorImpl(const char* file, int line, const char* func) {
    GLenum err;
    bool hasError = false;
    while ((err = glGetError()) != GL_NO_ERROR) {
        hasError = true;
        const char* errStr = "UNKNOWN";
        switch (err) {
            case GL_INVALID_ENUM:      errStr = "GL_INVALID_ENUM"; break;
            case GL_INVALID_VALUE:     errStr = "GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: errStr = "GL_INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY:     errStr = "GL_OUT_OF_MEMORY"; break;
            // GL_INVALID_FRAMEBUFFER_OPERATION 在 GLES2 中为扩展或核心的一部分
            #ifdef GL_INVALID_FRAMEBUFFER_OPERATION
            case GL_INVALID_FRAMEBUFFER_OPERATION: errStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
            #endif
            default: break;
        }
        std::fprintf(stderr, "[GL ERROR] %s:%d (%s) —— %s (0x%04X)\n", file, line, func, errStr, err);
    }
    if (!hasError) {
        std::printf("[GL OK] %s:%d (%s)\n", file, line, func);
    }
}

// -----------------------------------------------------------------------------
// GPU 信息查询
// -----------------------------------------------------------------------------

/**
 * 打印当前 GPU 及驱动信息
 *
 * 输出信息包括：
 * - GL_VENDOR:    GPU 厂商（如 ARM、Qualcomm、NVIDIA）
 * - GL_RENDERER:  渲染器名称（如 Mali-G78、Adreno 660）
 * - GL_VERSION:   OpenGL ES 版本及驱动版本
 * - GL_SHADING_LANGUAGE_VERSION: GLSL ES 版本
 *
 * 这些信息可用于：
 * - 判断设备是否支持某些高级特性
 * - 收集崩溃报告时的环境信息
 */
void PrintGPUInfo() {
    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* glslVersion = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

    std::printf("============================================================\n");
    std::printf("GPU 信息:\n");
    std::printf("  厂商 (VENDOR):                    %s\n", vendor ? vendor : "N/A");
    std::printf("  渲染器 (RENDERER):                %s\n", renderer ? renderer : "N/A");
    std::printf("  版本 (VERSION):                   %s\n", version ? version : "N/A");
    std::printf("  着色器版本 (GLSL VERSION):        %s\n", glslVersion ? glslVersion : "N/A");
    std::printf("============================================================\n");
}

/**
 * 检查当前上下文是否支持指定的 OpenGL ES 扩展
 *
 * @param extensionName 扩展名称，如 "GL_OES_texture_float_linear"
 * @return 支持返回 true，否则返回 false
 */
bool IsGLExtensionSupported(const char* extensionName) {
    if (!extensionName) return false;

    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!extensions) return false;

    std::string extList(extensions);
    // 简单子串查找（GLES 扩展字符串以空格分隔）
    return extList.find(extensionName) != std::string::npos;
}
