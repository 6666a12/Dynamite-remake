/**
 * @file judge_engine.cpp
 * @brief Dynamite 判定引擎核心实现
 *
 * 关键约束（绝对不可妥协）：
 * 1. 判定系统绝对禁止使用系统时钟，只能使用传入的 audio_now_ms
 * 2. 所有数组访问做边界检查
 * 3. 使用智能指针、RAII，禁止裸 new/delete
 *
   * 判定规则（Dynamite 标准）：
 * - 三级判定：Perfect（±59ms）/ Good（±90ms）/ Miss（>150ms 自动 Miss）
 * - 触摸匹配上限 ±90ms（window_good），超出不参与判定，等待 150ms Auto Miss
 * - SLIDE 判定：Catch-SLIDE（点击，参与防糊）+ Swipe-SLIDE（滑动，不参与防糊）
 * - SLIDE 窗口：|delta|≤±59ms→PERFECT，+59~+90ms→Late GOOD，<-59ms 不触发
  * - 长条(Hold)：头判+尾判，头判 Miss 则整根长条淡出
 *             按住过程中允许松开 500ms 内不断连
 * - 单触控判定：一个触控可判定多个同时的 note，不可判定多个不同时的 note
 * - 糊谱惩罚：多个不同时的 note，被同时的新触控试图判定 → 全部强制 Miss
 * - 垂直投影（Vertical Judge）：下半屏触摸投影到底判线用于 DOWN 判定
 * - 结算：P=100%, G=50%, M=0%，准确率实时计算
 * - 评价预留：FULL COMBO / ALL PERFECT
 */

#include "engine/judge_engine.h"
#include "engine/chart_parser.h"
#include "engine/input_manager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>

// =============================================================================
// 公有接口实现
// =============================================================================

void JudgeEngine::loadChart(const std::vector<NoteData>& notes) {
    active_notes_.clear();
    active_holds_.clear();
    frame_results_.clear();
    stats_ = Stats{};
    held_fingers_.clear();

    active_notes_.reserve(notes.size());

    for (const auto& nd : notes) {
        if (nd.type == NoteType::HOLD_BODY) {
            continue;
        }
        ActiveNote an;
        an.id          = nd.id;
        an.type        = static_cast<uint32_t>(nd.type);
        an.time_ms     = static_cast<int64_t>(nd.time_ms);
        an.side        = nd.side;
        an.duration_ms = nd.duration_ms;
        an.flags       = nd.flags;
        an.judged      = false;
        an.sub_id     = nd.sub_id;
        active_notes_.push_back(an);
    }

        for (auto& an : active_notes_) {
        if (an.type != static_cast<uint32_t>(NoteType::HOLD_HEAD)) {
            continue;
        }
        if (an.duration_ms == 0) {
            // 零时值 HOLD：通过 sub_id 找到对应的 SUB 计算时长
            for (const auto& tail : notes) {
                if (tail.id == an.sub_id && tail.type == NoteType::HOLD_TAIL) {
                    an.duration_ms = tail.time_ms - static_cast<uint64_t>(an.time_ms);
                    break;
                }
            }
        }
    }
}

void JudgeEngine::update(int64_t audio_now_ms, const std::vector<RawTouch>& touches) {
    frame_results_.clear();

    // 1. 垂直投影（Vertical Judge）：触摸在屏幕下半部时，
    //    如果 can_project=true 且 x 在 DOWN 区域内，生成一个 y 投影到
    //    底判线的副本用于 DOWN 判定（副本 can_project=false 防止递归投影）。
    //    原触摸保留用于 LEFT/RIGHT 判定。
    auto mutable_touches = touches;
    projectVerticalJudge(mutable_touches);

    

    // 计算"新按下"的手指集合
    std::unordered_set<int64_t> current_fingers;
    for (const auto& t : mutable_touches) {
        if (t.is_down) {
            current_fingers.insert(t.finger_id);
        }
    }

    std::unordered_set<int64_t> new_fingers;
    for (const auto& fid : current_fingers) {
        if (held_fingers_.find(fid) == held_fingers_.end()) {
            new_fingers.insert(fid);
        }
    }
    held_fingers_ = std::move(current_fingers);

    // 候选判定结构
    struct JudgeCandidate {
        size_t      note_index;
        JudgeType   type;
        int64_t     delta_ms;
        int64_t     judged_at_ms;
        bool        is_hold_tail;
    };
    std::vector<JudgeCandidate> candidates;

        // 追踪每个触控首次匹配的 note 时间，防止糊到不同时的 note
    std::unordered_map<int64_t, int64_t> touch_first_matched_time;

    // 遍历所有活跃 note 进行判定（note 优先）
    for (size_t idx = 0; idx < active_notes_.size(); ++idx) {
        auto& an = active_notes_[idx];
        if (an.judged) {
            continue;
        }
        if (an.type == static_cast<uint32_t>(NoteType::HOLD_BODY) ||
            an.type == static_cast<uint32_t>(NoteType::HOLD_TAIL)) {
            // HOLD_BODY 和 HOLD_TAIL 不参与正常判定流程，
            // HOLD_TAIL 由 processHold 统一管理（防 auto-miss 双判）
            continue;
        }

        // ---- Auto Miss 检测（所有类型通用）----
        if (audio_now_ms >= an.time_ms + window_miss) {
            an.judged = true;
            frame_results_.push_back({
                JudgeType::MISS,
                audio_now_ms - an.time_ms,
                an.id,
                an.side,
                audio_now_ms,
                false
            });
            updateStats(JudgeType::MISS);
            // HOLD_HEAD 自动 MISS 时仍需创建 HoldState 以便尾判
            if (an.type == static_cast<uint32_t>(NoteType::HOLD_HEAD)) {
                HoldState hs{};
                hs.note_id      = an.id;
                hs.side         = an.side;
                hs.head_time_ms = an.time_ms;
                hs.tail_time_ms = an.time_ms + static_cast<int64_t>(an.duration_ms);
                hs.last_press_ms = audio_now_ms;
                hs.is_held       = false;
                hs.head_judged   = true;
                hs.head_result   = JudgeType::MISS;
                active_holds_.push_back(hs);
            }
            continue;
        }

        // ============================================================
        // SLIDE 判定分支（融入主循环但使用独立的判定逻辑）
        // ============================================================
        if (an.type == static_cast<uint32_t>(NoteType::SLIDE)) {
            for (const auto& touch : mutable_touches) {
                if (!touch.is_down) continue;
                if (!isTouchInSide(touch, an.side)) continue;

                const int64_t delta_ms = audio_now_ms - an.time_ms;

                // SLIDE 判定：early 方向只有 perfect（|delta| <= window_perfect）
                // 即 delta < -window_perfect 时不触发任何判定
                if (delta_ms < -window_perfect) continue;

                // delta 在可判窗口内 [-window_perfect, +window_good]
                if (delta_ms <= window_good) {
                    const JudgeType jt = judgeSlideTiming(delta_ms);
                    an.judged = true;

                    // 判断是否为 Catch-SLIDE（点击判定）
                    // 条件：手指是新按下的（new_finger）+ 且按下时 delta >= -window_perfect
                    bool is_new_finger = (new_fingers.find(touch.finger_id) != new_fingers.end());
                    bool is_catch = is_new_finger && (delta_ms >= -window_perfect);

                    if (is_catch) {
                        // Catch-SLIDE：入 candidates，参与防糊
                        // 记录触控首次匹配时间（防糊：一个触控只能判同一个 time_ms 的 notes）
                        auto it = touch_first_matched_time.find(touch.finger_id);
                        if (it != touch_first_matched_time.end()) {
                            if (it->second != an.time_ms) {
                                // 该触控已匹配不同时的 note，跳过
                                an.judged = false; // 撤销标记
                                continue;
                            }
                        } else {
                            touch_first_matched_time[touch.finger_id] = an.time_ms;
                        }
                        candidates.push_back({idx, jt, delta_ms, audio_now_ms, false});
                    } else {
                        // Swipe-SLIDE：直接判，不经过 candidates（不参与防糊）
                        frame_results_.push_back({
                            jt, delta_ms, an.id, an.side, audio_now_ms, false
                        });
                        updateStats(jt);
                    }
                    break; // 一个 note 只判定一次
                }
            }
            continue; // SLIDE 处理完毕，跳到下一个 note
        }

        // ============================================================
        // TAP / HOLD_HEAD / MULTI 判定（HOLD_TAIL 已在上方 skip）
        // ============================================================
        // 触摸匹配：跳过 SLIDE（已在上面处理）
        if (an.type != static_cast<uint32_t>(NoteType::TAP) &&
            an.type != static_cast<uint32_t>(NoteType::HOLD_HEAD) &&
            an.type != static_cast<uint32_t>(NoteType::MULTI)) {
            continue;
        }

        for (const auto& touch : mutable_touches) {
            if (!touch.is_down) {
                continue;
            }
            if (new_fingers.find(touch.finger_id) == new_fingers.end()) {
                continue;
            }
            if (!isTouchInSide(touch, an.side)) {
                continue;
            }

            // 检查该触控是否已经匹配了不同时的 note
            auto it = touch_first_matched_time.find(touch.finger_id);
            if (it != touch_first_matched_time.end()) {
                if (it->second != an.time_ms) {
                    continue; // 已匹配不同时的 note，跳过
                }
                // 时间相同，允许继续匹配
            }

                        const int64_t delta_ms = audio_now_ms - an.time_ms;
            const int64_t abs_delta = std::llabs(delta_ms);

            // Dynamite 标准：触摸只匹配到 ±90ms，超过直接等 Auto Miss
            if (abs_delta <= window_good) {
                const JudgeType jt = judgeTiming(delta_ms);
                an.judged = true;
                candidates.push_back({idx, jt, delta_ms, audio_now_ms, false});
                
                // 记录该触控首次匹配的 note 时间
                if (it == touch_first_matched_time.end()) {
                    touch_first_matched_time[touch.finger_id] = an.time_ms;
                }
                break; // 一个 note 只判定一次
            }
        }
    }

    // 糊谱检测（多押惩罚机制）
    if (candidates.size() >= 2) {
        int64_t ref_time = active_notes_[candidates[0].note_index].time_ms;
        bool all_same_time = true;
        for (const auto& c : candidates) {
            if (active_notes_[c.note_index].time_ms != ref_time) {
                all_same_time = false;
                break;
            }
        }
        if (!all_same_time) {
            for (auto& c : candidates) {
                c.type = JudgeType::MISS;
            }
        }
    }

    // 统一应用候选判定结果
    for (const auto& c : candidates) {
        auto& an = active_notes_[c.note_index];

        if (an.type == static_cast<uint32_t>(NoteType::HOLD_HEAD) && !c.is_hold_tail) {
            // 无论是头判结果如何，都创建 HoldState
            // 头判 MISS 时尾判自动 MISS，非 MISS 时尾判沿用头判结果
            HoldState hs{};
            hs.note_id      = an.id;
            hs.side         = an.side;
            hs.head_time_ms = an.time_ms;
            hs.tail_time_ms = an.time_ms + static_cast<int64_t>(an.duration_ms);
            hs.last_press_ms = c.judged_at_ms;
            hs.is_held       = (c.type != JudgeType::MISS);  // 头判 MISS 时标记为未按住
            hs.head_judged   = true;
            hs.head_result   = c.type;
            active_holds_.push_back(hs);
        }

        frame_results_.push_back({
            c.type,
            c.delta_ms,
            an.id,
            an.side,
            c.judged_at_ms,
            c.is_hold_tail
        });
        updateStats(c.type);
    }

    processHold(audio_now_ms, touches);
}

JudgeType JudgeEngine::judgeTiming(int64_t delta_ms) const {
    const int64_t abs_delta = std::llabs(delta_ms);
    if (abs_delta <= window_perfect) {
        return JudgeType::PERFECT;
    }
    if (abs_delta <= window_good) {
        return JudgeType::GOOD;
    }
    return JudgeType::MISS;
}

JudgeType JudgeEngine::judgeSlideTiming(int64_t delta_ms) const {
        // SLIDE 判定窗口（Dynamite 标准）：
    //   |delta| <= window_perfect（±59ms）→ PERFECT（含 early perfect）
    //   +window_perfect < delta <= +window_good（+59~+90ms）→ Late GOOD
    //   其他 → MISS（delta < -59ms 不在判定触发范围内，等待后续 Swipe）
    if (std::llabs(delta_ms) <= window_perfect) {
        return JudgeType::PERFECT;
    }
    if (delta_ms > 0 && delta_ms <= window_good) {
        return JudgeType::GOOD;
    }
    return JudgeType::MISS;
}

void JudgeEngine::processHold(int64_t now_ms, const std::vector<RawTouch>& touches) {
    // 辅助：标记 HOLD_TAIL 在 active_notes_ 中为已判定，防止后续 auto-miss
    auto markTailJudged = [this](uint32_t head_id) {
        for (auto& an : active_notes_) {
            if (an.id == head_id && an.type == static_cast<uint32_t>(NoteType::HOLD_HEAD)) {
                uint32_t tail_id = an.sub_id;
                for (auto& tail : active_notes_) {
                    if (tail.id == tail_id &&
                        tail.type == static_cast<uint32_t>(NoteType::HOLD_TAIL)) {
                        tail.judged = true;
                        return;
                    }
                }
            }
        }
    };

    for (auto it = active_holds_.begin(); it != active_holds_.end(); ) {
        HoldState& hs = *it;

        bool has_touch_in_side = false;
        for (const auto& touch : touches) {
            if (touch.is_down && isTouchInSide(touch, hs.side)) {
                has_touch_in_side = true;
                hs.last_press_ms = now_ms;
                hs.is_held = true;
                break;
            }
        }

        if (!has_touch_in_side) {
            hs.is_held = false;
        }

        if (!hs.is_held && (now_ms - hs.last_press_ms > hold_break_tolerance_ms)) {
            frame_results_.push_back({
                JudgeType::MISS,
                now_ms - hs.tail_time_ms,
                hs.note_id,
                hs.side,
                now_ms,
                true
            });
            updateStats(JudgeType::MISS);
            markTailJudged(hs.note_id);
            it = active_holds_.erase(it);
            continue;
        }

        if (now_ms >= hs.tail_time_ms) {
            const JudgeType tail_result = hs.head_result;
            frame_results_.push_back({
                tail_result,
                now_ms - hs.tail_time_ms,
                hs.note_id,
                hs.side,
                now_ms,
                true
            });
            updateStats(tail_result);
            markTailJudged(hs.note_id);
            it = active_holds_.erase(it);
            continue;
        }

        ++it;
    }
}



void JudgeEngine::projectVerticalJudge(std::vector<RawTouch>& touches) const {
    // 垂直投影（Vertical Judge）：当触摸在屏幕下半部（y >= 0.5）且 x 在
    // DOWN 判定区域内时，生成一个 y 投影到底判线的副本用于 DOWN 判定。
    // 原触摸的 can_project 设为 false 防止递归投影。
    //
    // 投影结果：原触摸保留用于 LEFT/RIGHT 判定，
    //          新增触摸（y 投影到底判线）用于 DOWN 判定。
    // 判定区域常量（与 isTouchInSide 中的定义重复——有意为之：
    // 这些是文件局部常量，编译期完全内联，零运行时开销。集中到头文件会增加包含依赖）
    constexpr float kLeftJudgeX   = 108.0f / 1920.0f;
    constexpr float kRightJudgeX  = 1812.0f / 1920.0f;
    constexpr float kBottomJudgeY = 945.0f / 1080.0f;
    constexpr float kHalfScreenY  = 0.5f;

    std::vector<RawTouch> projections;
    projections.reserve(touches.size());

    for (auto& t : touches) {
        if (!t.is_down || !t.can_project) continue;
        if (t.y < kHalfScreenY) continue;           // 上半屏不投影
        if (t.x < kLeftJudgeX || t.x > kRightJudgeX) continue; // x 不在 DOWN 区域

        // 原触摸 can_project=false，不再参与后续投影
        t.can_project = false;

        // 生成投影触摸：x 不变（用于 DOWN 水平定位），y 投影到底判线
        // 使用不同的 finger_id（取负偏移）避免与原始触摸在防糊检查中冲突
        RawTouch proj = t;
        proj.finger_id = -t.finger_id - 1;  // 唯一负 ID，不与任何原始 ID 冲突
        proj.y = kBottomJudgeY;
        proj.can_project = false;  // 投影触摸不再参与额外投影
        projections.push_back(proj);
    }

    for (auto& p : projections) {
        touches.push_back(p);
    }
}

void JudgeEngine::updateStats(JudgeType type) {
    const int total_notes = stats_.perfect + stats_.good + stats_.miss + 1;

    switch (type) {
        case JudgeType::PERFECT:
            ++stats_.perfect;
            ++stats_.combo;
            stats_.score += 100;
            break;

        case JudgeType::GOOD:
            ++stats_.good;
            ++stats_.combo;
            stats_.score += 50;
            break;

        case JudgeType::MISS:
            ++stats_.miss;
            stats_.combo = 0;
            break;
    }

    if (stats_.combo > stats_.max_combo) {
        stats_.max_combo = stats_.combo;
    }

    const double weighted = static_cast<double>(stats_.perfect) * 1.0 +
                            static_cast<double>(stats_.good)    * 0.5;
    stats_.accuracy = (weighted / static_cast<double>(total_notes)) * 100.0;

    stats_.is_full_combo = (stats_.miss == 0);
    stats_.is_all_perfect = (stats_.good == 0 && stats_.miss == 0);
}

bool JudgeEngine::isTouchInSide(const RawTouch& touch, SideType side) const {
    // Dynamite 真实判定区域（二维矩形，归一化坐标 0.0~1.0）:
    //   LEFT 判线:  x=108px → 108/1920=0.05625, 判线半宽=24/1920=0.0125
    //   RIGHT 判线: x=1812px → 1812/1920=0.94375, 判线半宽=24/1920=0.0125
    //   BOTTOM 判线: y=945px → 945/1080=0.875, 判线半高=24/1080=0.0222
    //   DOWN 区域: x 在左右判线之间, y 在屏幕下半部(>=0.5)到底判线附近
    constexpr float kLeftJudgeX   = 108.0f / 1920.0f;
    constexpr float kRightJudgeX  = 1812.0f / 1920.0f;
    constexpr float kHalfWidth    = 24.0f / 1920.0f;
    constexpr float kHalfScreenY  = 0.5f;

    switch (side) {
        case SideType::LEFT:
            return (touch.x >= kLeftJudgeX - kHalfWidth &&
                    touch.x <= kLeftJudgeX + kHalfWidth &&
                    touch.y >= 0.0f && touch.y <= 1.0f);

        case SideType::DOWN:
            return (touch.x >= kLeftJudgeX &&
                    touch.x <= kRightJudgeX &&
                    touch.y >= kHalfScreenY &&
                    touch.y <= 1.0f);

        case SideType::RIGHT:
            return (touch.x >= kRightJudgeX - kHalfWidth &&
                    touch.x <= kRightJudgeX + kHalfWidth &&
                    touch.y >= 0.0f && touch.y <= 1.0f);

        default:
            return false;
    }
}
