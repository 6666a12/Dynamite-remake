#pragma once
#include <vector>
#include <cstdint>

// 前向声明
struct RawTouch;
class RenderBatch;

/**
 * 场景基类 —— 状态机模式
 */
class SceneBase {
public:
    virtual ~SceneBase() = default;

    virtual void init() {}
    virtual void enter() {}
    virtual void exit() {}

    // 音频时间驱动更新
    virtual void update(int64_t audio_now_ms) {}

    // 渲染
    virtual void render(RenderBatch& batch, int64_t audio_now_ms) {}

    // 输入处理
    virtual void handleInput(const std::vector<RawTouch>& touches, int64_t audio_now_ms) {}

    // 场景切换请求
    enum class Transition { NONE, PUSH, POP, REPLACE };
    struct TransitionRequest {
        Transition type = Transition::NONE;
        int target_scene_id = -1;
    };
    TransitionRequest transitionRequest() const { return transition_request_; }
    void clearTransitionRequest() { transition_request_ = {}; }

protected:
    TransitionRequest transition_request_;
};

enum class SceneID {
    LOGIN = 0,
    MAIN_MENU,
    SONG_SELECT,
    CHART_DETAIL,
    GAMEPLAY,
    RESULT,
    SETTINGS,
    EVENT,
    SKILL_SET,
    COUNT
};
