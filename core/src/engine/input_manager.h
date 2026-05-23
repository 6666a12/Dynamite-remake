#pragma once
#include <vector>
#include <cstdint>
#include <optional>
#include "judge_engine.h" // for SideType

// 前向声明 SDL 事件
union SDL_Event;

/**
 * 输入管理器 —— SDL3 多指触控封装
 * 
 * 设计决策：
 * 1. 所有触摸事件在 SDL 回调中立即标记 audio_now_ms 时间戳
 * 2. 禁止异步缓冲输入事件，防止输入延迟
 * 3. 支持查询指定区域内的最佳触摸（用于轨道判定）
 */
struct RawTouch {
    int64_t finger_id;   // SDL 提供的触摸 ID
    float   x, y;        // 归一化 0.0 ~ 1.0（以屏幕左上角为原点）
    int64_t timestamp_ms;// 事件时间戳（由上层在 SDL 回调中标记为 audio_clock.nowMs()）
    bool    is_down;     // true = 按下/移动中，false = 抬起
    bool    is_new;      // 本帧新按下
    bool    can_project = true; // 是否允许垂直投影（用于 vertical judge 机制）
};

class InputManager {
public:
    void handleSDLEvent(const SDL_Event& e, int64_t audio_now_ms);
    const std::vector<RawTouch>& touches() const { return touches_; }

    // 查询指定区域内最佳触摸（用于轨道判定）
    std::optional<RawTouch> queryTouchInRegion(
        float cx, float cy, float radius, int64_t before_ms
    ) const;

    // 三侧判定区域映射（根据屏幕比例动态计算）
    struct SideRegion {
        float x, y, w, h; // 归一化矩形
    };
    SideRegion getSideRegion(SideType side, float screen_w, float screen_h) const;

private:
    std::vector<RawTouch> touches_;
    std::vector<RawTouch> prev_touches_;
};
