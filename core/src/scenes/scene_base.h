#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <algorithm>
#include <SDL3/SDL.h>

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

    // 当前时间基准（默认用系统时钟，Gameplay 场景用音频时钟覆盖）
    virtual int64_t currentTimeMs() const { return static_cast<int64_t>(SDL_GetTicks()); }

    // 应用前后台切换（仅 Gameplay 场景需要暂停/恢复音频）
    virtual void onPause() {}
    virtual void onResume() {}

    // 场景切换请求
    // 统一布局常量 (各场景共享)
    static constexpr float kHeaderH = 72.0f;   // 统一顶栏高度
    static constexpr float kFooterH = 64.0f;   // 统一底栏高度
    static constexpr int kDesignW = 1920;
    static constexpr int kDesignH = 1080;
    
    enum class Transition { NONE, PUSH, POP, REPLACE };
    
    // 场景间数据载荷（消除全局变量）
    struct GameplayResultPayload {
        int perfect = 0;
        int good = 0;
        int miss = 0;
        int max_combo = 0;
        int score = 0;
        double accuracy = 0.0;
        bool is_full_combo = false;
        bool is_all_perfect = false;
        std::string song_title;
        std::string difficulty;
        std::string chart_path;    // 谱面文件路径 (重试用)
        std::string audio_path;    // 音频文件路径 (重试用)
    };
    
    struct TransitionRequest {
        Transition type = Transition::NONE;
        int target_scene_id = -1;
        GameplayResultPayload payload;
    };
    TransitionRequest transitionRequest() const { return transition_request_; }
    void clearTransitionRequest() { transition_request_ = {}; }
    void setTransitionPayload(const GameplayResultPayload& p) { transition_request_.payload = p; }

protected:
    TransitionRequest transition_request_;
    
    // 全局斜纹滚动时间（毫秒），由各场景 update() 更新
    // Gameplay 场景保持为 0（静态斜纹），其他场景随时间滚动
    int64_t stripe_time_ms_ = 0;

    // ---- 触摸区工具（§14 约束 #17: 最小触摸区 ≥ 44dp） ----
    // 将给定矩形膨胀到至少 44dp（从中心扩展），然后检测点是否在内
    static constexpr float kMinTouchDp = 44.0f;

    static bool HitTest(float px, float py, float x, float y, float w, float h) {
        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;
        float hw = std::max(w * 0.5f, kMinTouchDp * 0.5f);
        float hh = std::max(h * 0.5f, kMinTouchDp * 0.5f);
        return px >= cx - hw && px <= cx + hw && py >= cy - hh && py <= cy + hh;
    }
};

enum class SceneID {
    LOGIN = 0,
    MAIN_MENU,
    SONG_SELECT,
    CHART_DETAIL,
    GAMEPLAY,
    RESULT,
    SHOP,
    SETTINGS,
    EVENT,
    SKILL_SET,
    COUNT
};
