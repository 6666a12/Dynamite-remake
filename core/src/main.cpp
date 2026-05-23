/**
 * Dynamite Rebuild Project --- C++ GameCore Entry
 * 
 * Platforms: Android / iOS / Desktop (debug)
 * Architecture: SDL3 + OpenGL ES 3.0 + miniaudio
 * Coordinate system: 1920x1080 design resolution, 16:9 letterbox viewport
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
#include "scenes/scene_gameplay_new.hpp"
#include "scenes/scene_result.h"
#include "scenes/scene_shop.h"
#include "bridge/go_bridge.h"
#include "utils/logger.h"
#include "utils/config_manager.h"

// Desktop development entry point
int main(int argc, char** argv) {
    (void)argc; (void)argv;

    // 1. Init SDL3
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // 2. Create window and GL context
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

    // VSync: enabled, frame rate synced to display refresh rate
    SDL_GL_SetSwapInterval(1);

    // 3. Init config manager (detects screen params on first launch)
    {
        std::string configPath;
#if defined(__ANDROID__)
        configPath = std::string(SDL_GetAndroidInternalStoragePath()) + "/config.json";
#else
        configPath = "./config.json";
#endif
        ConfigManager::instance().init(configPath);

        int detected_w = 0, detected_h = 0;
        float detected_refresh = 60.0f;
        SDL_GetWindowSize(window, &detected_w, &detected_h);
        SDL_DisplayMode mode;
        if (SDL_GetCurrentDisplayMode(0, &mode)) {
            detected_refresh = mode.refresh_rate;
        }
        ConfigManager::instance().setScreenWidth(detected_w);
        ConfigManager::instance().setScreenHeight(detected_h);
        ConfigManager::instance().setRefreshRate(static_cast<int>(detected_refresh));
        ConfigManager::instance().save();

        Logger::info("Screen: {}x{} @{}Hz", detected_w, detected_h, static_cast<int>(detected_refresh));
    }

    // 4. Init subsystems
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

    // 5. Init Go DataLayer (desktop mock)
    GoBridge::init("./game.db");

    // 6. Scene stack
    std::vector<std::unique_ptr<SceneBase>> scene_stack;

#if defined(__ANDROID__)
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

    // 7. Main loop
    bool running = true;
    while (running) {
        // Audio clock as single time base
#if defined(__ANDROID__)
        int64_t audio_now = static_cast<int64_t>(gameplay_scene->audioClock().nowMs());
#else
        int64_t audio_now = static_cast<int64_t>(audio.clock().nowMs());
#endif

        // --- Input collection ---
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

        // --- Scene update ---
        if (!scene_stack.empty()) {
            auto& current = scene_stack.back();
            current->handleInput(input.touches(), audio_now);
            current->update(audio_now);

            auto req = current->transitionRequest();
            if (req.type != SceneBase::Transition::NONE) {
                current->clearTransitionRequest();
                if (req.type == SceneBase::Transition::POP) {
                    current->exit();
                    scene_stack.pop_back();
                    if (!scene_stack.empty()) scene_stack.back()->enter();
                } else if (req.type == SceneBase::Transition::PUSH || req.type == SceneBase::Transition::REPLACE) {
                    if (req.type == SceneBase::Transition::REPLACE) {
                        current->exit();
                        scene_stack.pop_back();
                    } else {
                        current->exit();
                    }
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
        if (scene_stack.empty()) {
            running = false;
        }
#endif

        // --- Render ---
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
    }

    // 8. Cleanup
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
