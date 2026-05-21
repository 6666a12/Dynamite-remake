/**
 * Dynamite 重构项目 —— C++ GameCore 入口
 * 
 * 平台：Android / iOS / Desktop(开发调试用)
 * 架构：SDL3 + OpenGL ES 3.0 + miniaudio
 */

#include <SDL3/SDL.h>
#if defined(__ANDROID__)
#include <SDL3/SDL_main.h>
#endif
#if defined(__ANDROID__) || defined(__APPLE__)
#include <SDL3/SDL_opengles2.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <cstdio>
#include <memory>
#include <vector>

#include "engine/audio_engine.h"
#include "engine/judge_engine.h"
#include "engine/input_manager.h"
#include "engine/render_batch.h"
#include "engine/chart_parser.h"
#include "scenes/scene_base.h"
#include "scenes/scene_main_menu.h"
#include "scenes/scene_song_select.h"
#include "scenes/scene_gameplay.h"
#include "scenes/scene_result.h"
#include "scenes/scene_shop.h"
#include "bridge/go_bridge.h"
#include "utils/logger.h"

// 桌面开发调试入口
int main(int argc, char** argv) {
    (void)argc; (void)argv;

    // 1. 初始化 SDL3
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // 2. 创建窗口与 GL 上下文
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_Window* window = SDL_CreateWindow("Dynamite Rebuild",
        1920, 1080,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(0); // Android 上 VSync 可能异常阻塞，关闭后手动限帧

    // 3. 初始化子系统
    RenderBatch batch;
    batch.init();

    InputManager input;
    JudgeEngine judge;

#if !defined(__ANDROID__)
    AudioEngine audio;
    if (!audio.init(44100, 2, 128)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "AudioEngine init failed, running without audio");
    }
#endif

    // 4. 初始化 Go DataLayer（桌面模式 mock）
    GoBridge::init("./game.db");

    // 5. 场景栈
    std::vector<std::unique_ptr<SceneBase>> scene_stack;

#if defined(__ANDROID__)
    // Android 初步 APK：直接加载 GIGA 谱面，播放完后自动退出
    auto gameplay = std::make_unique<SceneGameplay>();
    scene_stack.push_back(std::move(gameplay));
#else
    scene_stack.push_back(std::make_unique<SceneMainMenu>());
#endif
    scene_stack.back()->init();
    scene_stack.back()->enter();

#if defined(__ANDROID__)
    std::string internalPath = SDL_GetAndroidInternalStoragePath();
    if (!internalPath.empty() && internalPath.back() != '/') {
        internalPath += '/';
    }
    auto* gameplay_scene = static_cast<SceneGameplay*>(scene_stack.back().get());
    if (gameplay_scene) {
        gameplay_scene->loadChart(
            internalPath + "songs/song_sample/chart_giga.chart",
            internalPath + "songs/song_sample/bgm.mp3"
        );
    }
#endif

    // 6. 主循环
    bool running = true;
    while (running) {
        // 音频时钟作为唯一时间基准
#if defined(__ANDROID__)
        int64_t audio_now = static_cast<int64_t>(gameplay_scene->audioClock().nowMs());
#else
        int64_t audio_now = static_cast<int64_t>(audio.clock().nowMs());
#endif

        // --- 输入收集 ---
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
            }
#if defined(__ANDROID__)
            if (e.type == SDL_EVENT_DID_ENTER_BACKGROUND) {
                gameplay_scene->pauseAudio();
            }
            if (e.type == SDL_EVENT_WILL_ENTER_FOREGROUND) {
                gameplay_scene->resumeAudio();
            }
#endif
            input.handleSDLEvent(e, audio_now);
        }

        // --- 场景更新 ---
        if (!scene_stack.empty()) {
            auto& current = scene_stack.back();
            current->handleInput(input.touches(), audio_now);
            current->update(audio_now);

            // 处理场景切换请求
            auto req = current->transitionRequest();
            if (req.type != SceneBase::Transition::NONE) {
                current->clearTransitionRequest();
                if (req.type == SceneBase::Transition::POP) {
                    current->exit();
                    scene_stack.pop_back();
                    if (!scene_stack.empty()) scene_stack.back()->enter();
                } else if (req.type == SceneBase::Transition::PUSH || req.type == SceneBase::Transition::REPLACE) {
                    // PUSH: 前一个 scene exit 后压栈
                    // REPLACE: 弹出当前 scene 再压栈新 scene
                    if (req.type == SceneBase::Transition::REPLACE) {
                        current->exit();
                        scene_stack.pop_back();
                    } else {
                        current->exit();
                    }
                    // 根据 target_scene_id 创建新场景
                    switch (static_cast<SceneID>(req.target_scene_id)) {
                        case SceneID::MAIN_MENU:
                            scene_stack.push_back(std::make_unique<SceneMainMenu>());
                            break;
                        case SceneID::SONG_SELECT:
                            scene_stack.push_back(std::make_unique<SceneSongSelect>());
                            break;
                        case SceneID::GAMEPLAY:
                            scene_stack.push_back(std::make_unique<SceneGameplay>());
                            break;
                        case SceneID::RESULT:
                            scene_stack.push_back(std::make_unique<SceneResult>());
                            break;
                        case SceneID::SHOP:
                            scene_stack.push_back(std::make_unique<SceneShop>());
                            break;
                        case SceneID::EVENT:
                        case SceneID::SKILL_SET:
                        case SceneID::SETTINGS:
                            scene_stack.push_back(std::make_unique<SceneMainMenu>());
                            break;
                        default:
                            break;
                    }
                    if (!scene_stack.empty()) {
                        scene_stack.back()->init();
                        scene_stack.back()->enter();
                    }
                }
            }
        }
#if defined(__ANDROID__)
        // Android 初步 APK：场景栈为空时自动退出
        if (scene_stack.empty()) {
            running = false;
        }
#endif

        // --- 渲染 ---
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.06f, 0.06f, 0.06f, 1.0f); // #0F0F0F
        glClear(GL_COLOR_BUFFER_BIT);

        batch.beginFrame(w, h);
        if (!scene_stack.empty()) {
            scene_stack.back()->render(batch, audio_now);
        }
        batch.endFrame();

        SDL_GL_SwapWindow(window);

        // 手动限帧 ~60fps，避免 CPU 占满
        static uint32_t last_tick = SDL_GetTicks();
        uint32_t frame_time = SDL_GetTicks() - last_tick;
        if (frame_time < 16) {
            SDL_Delay(16 - frame_time);
        }
        last_tick = SDL_GetTicks();
    }

    // 7. 清理
    scene_stack.clear();
#if !defined(__ANDROID__)
    audio.shutdown();
#endif
    batch.shutdown();
    SDL_GL_DestroyContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
