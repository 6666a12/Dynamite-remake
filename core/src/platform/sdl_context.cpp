/**
 * Dynamite 重构项目 —— SDL3 平台上下文封装
 *
 * 功能：
 * - 统一封装 SDL3 窗口创建（支持 Android / iOS / Desktop）
 * - OpenGL ES 上下文管理
 * - 事件循环封装
 *
 * 设计原则：
 * - 隐藏平台差异，向上层提供统一接口
 * - 支持生命周期管理（init / shutdown）
 */

#include <SDL3/SDL.h>
#if defined(__ANDROID__) || defined(__APPLE__)
#include <SDL3/SDL_opengles2.h>
#else
#include <GL/gl.h>
#endif
#include <cstdio>
#include <string>

// -----------------------------------------------------------------------------
// SDLContext 结构体定义
// -----------------------------------------------------------------------------
/**
 * SDLContext 保存 SDL 运行期核心对象
 *
 * 包括：
 * - window: SDL 窗口句柄
 * - glContext: OpenGL ES 上下文
 * - running: 主循环运行标志
 */
struct SDLContext {
    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;
    bool running = false;
    int windowWidth = 1080;
    int windowHeight = 1920;
};

static SDLContext g_ctx; // 全局单例（简化设计，未来可改为依赖注入）

// -----------------------------------------------------------------------------
// 初始化与销毁
// -----------------------------------------------------------------------------

/**
 * 初始化 SDL 子系统与窗口
 *
 * @param title 窗口标题
 * @param w 窗口宽度（像素）
 * @param h 窗口高度（像素）
 * @return 初始化成功返回 true
 *
 * 平台差异处理：
 * - Desktop: 创建可调整大小的窗口
 * - Android/iOS: SDL 自动创建全屏窗口，w/h 建议值仅供参考
 */
bool SDL_InitContext(const char* title, int w, int h) {
    // 1. 初始化 SDL 核心子系统：视频、事件、音频
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init 失败: %s", SDL_GetError());
        return false;
    }

    // 2. 配置 OpenGL ES 3.0 上下文属性
    // Android 与 iOS 默认支持 GLES；Desktop 通过 ANGLE 或原生驱动模拟
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // 3. 创建窗口
    // SDL_WINDOW_OPENGL: 窗口关联 OpenGL 上下文
    // SDL_WINDOW_RESIZABLE: 桌面端允许调整大小（移动端无实际影响）
    // SDL_WINDOW_HIGH_PIXEL_DENSITY: 支持 Retina / 高 DPI 屏幕
    g_ctx.window = SDL_CreateWindow(title, w, h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_ctx.window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow 失败: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    // 4. 创建 GL 上下文
    g_ctx.glContext = SDL_GL_CreateContext(g_ctx.window);
    if (!g_ctx.glContext) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GL_CreateContext 失败: %s", SDL_GetError());
        SDL_DestroyWindow(g_ctx.window);
        SDL_Quit();
        return false;
    }

    // 5. 启用垂直同步（VSync），目标 60fps
    // 若设备不支持 VSync，SDL_GL_SetSwapInterval 返回 -1，但不致命
    if (SDL_GL_SetSwapInterval(1) < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "无法启用 VSync: %s", SDL_GetError());
    }

    // 6. 记录初始尺寸
    SDL_GetWindowSize(g_ctx.window, &g_ctx.windowWidth, &g_ctx.windowHeight);
    g_ctx.running = true;

    SDL_Log("[SDLContext] 初始化完成: %dx%d", g_ctx.windowWidth, g_ctx.windowHeight);
    return true;
}

/**
 * 销毁 SDL 上下文，释放所有资源
 *
 * 调用顺序：
 * 1. 销毁 GL 上下文
 * 2. 销毁窗口
 * 3. 退出 SDL 子系统
 */
void SDL_ShutdownContext() {
    g_ctx.running = false;

    if (g_ctx.glContext) {
        SDL_GL_DestroyContext(g_ctx.glContext);
        g_ctx.glContext = nullptr;
    }
    if (g_ctx.window) {
        SDL_DestroyWindow(g_ctx.window);
        g_ctx.window = nullptr;
    }
    SDL_Quit();

    SDL_Log("[SDLContext] 已销毁");
}

// -----------------------------------------------------------------------------
// 事件循环封装
// -----------------------------------------------------------------------------

/**
 * 轮询并处理 SDL 事件
 *
 * 处理的事件类型：
 * - SDL_EVENT_QUIT: 窗口关闭请求（如点击关闭按钮、系统返回键）
 * - SDL_EVENT_WINDOW_RESIZED: 窗口尺寸变化，更新内部记录
 *
 * @return 若收到退出事件，返回 false；否则返回 true
 */
bool SDL_PollEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT:
                // 桌面端关闭窗口、移动端系统退出信号
                g_ctx.running = false;
                return false;

            case SDL_EVENT_WINDOW_RESIZED:
                // 窗口尺寸变化，更新缓存的尺寸信息
                g_ctx.windowWidth = e.window.data1;
                g_ctx.windowHeight = e.window.data2;
                SDL_Log("[SDLContext] 窗口尺寸变化: %dx%d", g_ctx.windowWidth, g_ctx.windowHeight);
                break;

            case SDL_EVENT_KEY_DOWN:
                // 预留：可在此分发键盘事件给 InputManager
                break;

            case SDL_EVENT_FINGER_DOWN:
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_MOTION:
                // 预留：触摸事件已由 InputManager 直接读取
                break;

            default:
                break;
        }
    }
    return g_ctx.running;
}

/**
 * 交换前后缓冲，将渲染结果呈现到屏幕
 */
void SDL_SwapBuffers() {
    if (g_ctx.window) {
        SDL_GL_SwapWindow(g_ctx.window);
    }
}

// -----------------------------------------------------------------------------
// 查询接口
// -----------------------------------------------------------------------------

/**
 * 获取当前窗口尺寸
 *
 * @param outW 输出宽度
 * @param outH 输出高度
 */
void SDL_GetContextSize(int* outW, int* outH) {
    if (outW) *outW = g_ctx.windowWidth;
    if (outH) *outH = g_ctx.windowHeight;
}

/**
 * 查询主循环是否仍在运行
 */
bool SDL_IsRunning() {
    return g_ctx.running;
}
