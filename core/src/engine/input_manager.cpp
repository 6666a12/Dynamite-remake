/**
 * @file input_manager.cpp
 * @brief SDL3 多指触控输入管理器实现
 *
 * 设计要点：
 * 1. 所有触摸事件在 SDL 回调中立即处理，不经过任何异步队列，杜绝输入延迟
 * 2. touches_ 向量始终反映当前实际按下的触摸点（FINGER_UP 后移除）
 * 3. 支持查询指定区域内的最佳触摸，用于三侧轨道判定
 * 4. 三侧布局采用归一化坐标：LEFT(左30%), DOWN(底中40%), RIGHT(右30%)
 */

#include "engine/input_manager.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

/**
 * @brief 处理单个 SDL 事件，提取触摸信息并更新内部状态
 * @param e           SDL 事件联合体
 * @param audio_now_ms 当前音频时钟毫秒数（由上层传入，作为触摸时间戳）
 *
 * 事件处理策略：
 * - FINGER_DOWN：新增 RawTouch，标记 is_new = true（表示本帧新按下）
 * - FINGER_MOTION：更新已有触摸坐标，标记 is_new = false
 * - FINGER_UP：从向量中移除该触摸（而非仅标记 is_down = false），
 *   确保 JudgeEngine 在后续 update 中不会看到已抬起的触摸
 *
 * 线程安全：
 * - 本函数仅在主线程的 SDL_PollEvent 循环中调用，无并发访问问题
 */
void InputManager::handleSDLEvent(const SDL_Event& e, int64_t audio_now_ms) {
    switch (e.type) {
        case SDL_EVENT_FINGER_DOWN: {
            RawTouch touch{};
            touch.finger_id    = static_cast<int64_t>(e.tfinger.fingerID);
            touch.x            = e.tfinger.x;   // SDL3 已归一化到 0.0~1.0
            touch.y            = e.tfinger.y;
            touch.timestamp_ms = audio_now_ms;   // 使用音频时钟作为唯一时间基准
            touch.is_down      = true;
            touch.is_new       = true;           // 标记为"本帧新按下"
            touches_.push_back(touch);
            break;
        }

        case SDL_EVENT_FINGER_MOTION: {
            const int64_t fid = static_cast<int64_t>(e.tfinger.fingerID);
            bool found = false;

            // 查找并更新对应手指的坐标
            for (auto& t : touches_) {
                if (t.finger_id == fid) {
                    t.x            = e.tfinger.x;
                    t.y            = e.tfinger.y;
                    t.timestamp_ms = audio_now_ms;
                    t.is_down      = true;
                    t.is_new       = false; // 移动事件不再视为新按下
                    found          = true;
                    break;
                }
            }

            // 防御性处理：理论上应先收到 DOWN 再收到 MOTION，
            // 若出现异常丢失 DOWN 的情况，自动补录一条
            if (!found) {
                RawTouch touch{};
                touch.finger_id    = fid;
                touch.x            = e.tfinger.x;
                touch.y            = e.tfinger.y;
                touch.timestamp_ms = audio_now_ms;
                touch.is_down      = true;
                touch.is_new       = false;
                touches_.push_back(touch);
            }
            break;
        }

        case SDL_EVENT_FINGER_UP: {
            const int64_t fid = static_cast<int64_t>(e.tfinger.fingerID);

            // 从向量中直接移除已抬起的触摸，避免残留到下一帧
            touches_.erase(
                std::remove_if(touches_.begin(), touches_.end(),
                    [fid](const RawTouch& t) { return t.finger_id == fid; }),
                touches_.end()
            );
            break;
        }

        default:
            // 非触摸事件直接忽略
            break;
    }
}

/**
 * @brief 查询指定圆形区域内的最佳触摸
 * @param cx        圆心 x（归一化 0.0~1.0）
 * @param cy        圆心 y（归一化 0.0~1.0）
 * @param radius    半径（归一化坐标系中的距离）
 * @param before_ms 只考虑时间戳 <= before_ms 的触摸（用于回溯判定）
 * @return std::optional<RawTouch> 若区域内无有效触摸则返回 std::nullopt
 *
 * 选取策略：
 * - 优先返回距离圆心最近的触摸
 * - 过滤掉时间戳晚于 before_ms 的触摸（防止未来事件被错误回溯）
 */
std::optional<RawTouch> InputManager::queryTouchInRegion(
    float cx, float cy, float radius, int64_t before_ms
) const {
    // 防御性检查：半径必须为正数
    if (radius <= 0.0f) {
        return std::nullopt;
    }

    std::optional<RawTouch> best;
    float best_dist_sq = radius * radius; // 预计算半径平方，避免开方

    for (const auto& t : touches_) {
        // 只考虑当前处于按下状态的触摸（理论上 touches_ 中全是按下的）
        if (!t.is_down) {
            continue;
        }

        // 时间戳过滤：防止使用"未来"的触摸事件
        if (t.timestamp_ms > before_ms) {
            continue;
        }

        // 计算触摸点与圆心的欧氏距离平方
        const float dx = t.x - cx;
        const float dy = t.y - cy;
        const float dist_sq = dx * dx + dy * dy;

        // 选取距离最近且在半径内的触摸
        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            best = t;
        }
    }

    return best;
}

/**
 * @brief 获取指定侧在三侧布局中的归一化矩形区域
 * @param side      侧类型：LEFT / DOWN / RIGHT
 * @param screen_w  屏幕像素宽度（仅用于比例计算，实际返回归一化值）
 * @param screen_h  屏幕像素高度（当前布局为横向分割，高度始终为全高）
 * @return SideRegion 归一化矩形 {x, y, w, h}
 *
 * 布局规则：
 * - LEFT：  x=0.0,  w=0.3（左 30%）
 * - DOWN：  x=0.3,  w=0.4（底中 40%）
 * - RIGHT： x=0.7,  w=0.3（右 30%）
 * - 三侧均占满整个屏幕高度：y=0.0, h=1.0
 *
 * 说明：
 * - screen_w / screen_h 参数保留用于未来支持非满屏判定线或竖屏适配
 * - 当前实现中 y 和 h 固定为 0.0 和 1.0
 */
InputManager::SideRegion InputManager::getSideRegion(SideType side, float screen_w, float screen_h) const {
    (void)screen_w; // 当前布局为纯比例分割，与绝对像素无关
    (void)screen_h;

    SideRegion region{};
    region.y = 0.0f;
    region.h = 1.0f;

    switch (side) {
        case SideType::LEFT:
            region.x = 0.0f;
            region.w = 0.30f;
            break;

        case SideType::DOWN:
            region.x = 0.30f;
            region.w = 0.40f;
            break;

        case SideType::RIGHT:
            region.x = 0.70f;
            region.w = 0.30f;
            break;

        default:
            // 防御性处理：未知侧返回空区域
            region.x = 0.0f;
            region.w = 0.0f;
            region.h = 0.0f;
            break;
    }

    return region;
}
