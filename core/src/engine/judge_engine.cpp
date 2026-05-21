/**
 * @file judge_engine.cpp
 * @brief Dynamite 判定引擎核心实现
 *
 * 关键约束（绝对不可妥协）：
 * 1. 判定系统绝对禁止使用系统时钟，只能使用传入的 audio_now_ms
 * 2. 所有数组访问做边界检查
 * 3. 使用智能指针、RAII，禁止裸 new/delete
 *
 * 判定规则：
 * - 三级判定：Perfect（±25ms）/ Good（±55ms）/ Miss（>150ms 自动 Miss）
 * - 长条(Hold)：头判+尾判，头判 Miss 则整根长条淡出
 *             按住过程中允许松开 500ms 内不断连
 * - 双押合并：相邻侧判定区域重叠 30%，点击中间区域可同时判定两侧
 * - 单触控判定：一个触控可判定多个同时的 note，不可判定多个不同时的 note
 * - 糊谱惩罚：多个不同时的 note，被同时的新触控试图判定 → 全部强制 Miss
 * - 结算：P=100%, G=50%, M=0%，准确率实时计算
 * - 评价预留：FULL COMBO / ALL PERFECT / ONE GOOD / ONE MISS / DOUBLE ONE
 * - R 值系统预留接口
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

    // 双押合并
    auto mutable_touches = touches;
    processChordMerge(mutable_touches);

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

    // 遍历所有活跃 note 进行头判（note 优先）
    for (size_t idx = 0; idx < active_notes_.size(); ++idx) {
        auto& an = active_notes_[idx];
        if (an.judged) {
            continue;
        }
        if (an.type == static_cast<uint32_t>(NoteType::HOLD_BODY)) {
            continue;
        }

        // Auto Miss 检测
        // Auto Miss 检测
        if (audio_now_ms > an.time_ms + window_miss) {
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
        // 触摸匹配
        if (an.type != static_cast<uint32_t>(NoteType::TAP) &&
            an.type != static_cast<uint32_t>(NoteType::HOLD_HEAD) &&
            an.type != static_cast<uint32_t>(NoteType::SLIDE) &&
            an.type != static_cast<uint32_t>(NoteType::MULTI) &&
            an.type != static_cast<uint32_t>(NoteType::HOLD_TAIL)) {
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

            if (abs_delta <= window_miss) {
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

void JudgeEngine::processHold(int64_t now_ms, const std::vector<RawTouch>& touches) {
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
            it = active_holds_.erase(it);
            continue;
        }

        ++it;
    }
}

void JudgeEngine::processChordMerge(std::vector<RawTouch>& touches) {
    const float left_down_low  = 0.30f - 0.30f * chord_merge_overlap;
    const float left_down_high = 0.30f + 0.40f * chord_merge_overlap;
    const float down_right_low  = 0.70f - 0.40f * chord_merge_overlap;
    const float down_right_high = 0.70f + 0.30f * chord_merge_overlap;

    std::vector<RawTouch> extras;
    extras.reserve(touches.size());

    for (const auto& t : touches) {
        if (!t.is_down) {
            continue;
        }

        if (t.x >= left_down_low && t.x <= left_down_high) {
            RawTouch copy = t;
            copy.x = 0.50f;
            extras.push_back(copy);
        }

        if (t.x >= down_right_low && t.x <= down_right_high) {
            RawTouch copy = t;
            copy.x = 0.85f;
            extras.push_back(copy);
        }
    }

    for (auto& e : extras) {
        touches.push_back(e);
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
    const float x = touch.x;

    switch (side) {
        case SideType::LEFT:
            return (x >= 0.0f && x < 0.30f);

        case SideType::DOWN:
            return (x >= 0.30f && x < 0.70f);

        case SideType::RIGHT:
            return (x >= 0.70f && x <= 1.0f);

        default:
            return false;
    }
}
