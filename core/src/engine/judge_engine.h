#pragma once
#include <vector>
#include <cstdint>
#include <optional>
#include <unordered_set>

/**
 * Dynamite 判定引擎 —— 三侧下落式音游核心
 * 
 * 时间基准：必须使用 AudioClock::nowMs()，严禁使用系统时钟
 */

enum class JudgeType : uint8_t { PERFECT, GOOD, MISS };
enum class SideType : uint8_t { LEFT = 0, DOWN = 1, RIGHT = 2 };

struct JudgeResult {
    JudgeType type;
    int64_t   delta_ms;      // 偏差（可正可负，显示快慢）
    uint32_t  note_id;
    SideType  side;
    int64_t   judged_at_ms;  // 判定时刻（音频时钟）
    bool      is_hold_tail;  // 是否为长条尾判
};

/**
 * 判定引擎内部使用的活跃 Note 表示
 * 使用 uint32_t 存储 type 以避免与 chart_parser.h 的循环包含
 */
struct ActiveNote {
    uint32_t id;
    uint32_t type;      // 对应 NoteType 的底层整数值
    int64_t time_ms;
    SideType side;
    uint64_t duration_ms;
    uint32_t flags;
    uint32_t sub_id;   // HOLD_HEAD 指向 HOLD_TAIL 的 id
    bool judged = false;
};

struct HoldState {
    uint32_t note_id;
    SideType side;
    int64_t  head_time_ms;
    int64_t  tail_time_ms;
    int64_t  last_press_ms;  // 最近一次按下时刻
    bool     is_held;        // 当前是否按住
    bool     head_judged;    // 头判是否已完成
    JudgeType head_result;   // 头判结果
};

class JudgeEngine {
public:
    // 判定窗口（毫秒），从 config 读取，可动态调整
    int32_t window_perfect = 25;  // ±25ms
    int32_t window_good    = 55;  // ±55ms
    int32_t window_miss    = 150; // >150ms 自动 Miss

    // 长条容错：允许松开 500ms 内不断连
    int32_t hold_break_tolerance_ms = 500;

    // 双押合并：相邻轨道判定区域重叠 30% 时，点击中间区域可同时判定
    float chord_merge_overlap = 0.30f;

    // 初始化谱面
    void loadChart(const std::vector<struct NoteData>& notes);

    // 每帧调用（音频时间驱动）
    void update(int64_t audio_now_ms, const std::vector<struct RawTouch>& touches);

    const std::vector<JudgeResult>& frameResults() const { return frame_results_; }

        // 结算统计
    struct Stats {
        int perfect = 0, good = 0, miss = 0;
        int max_combo = 0;
        int combo = 0;
        bool is_full_combo = false;
        bool is_all_perfect = false;
        double accuracy = 0.0; // P=100%, G=50%, M=0%
        int score = 0;
    };
    Stats currentStats() const { return stats_; }

    // 获取当前 hold 状态（供渲染层查询是否正在按住）
    const std::vector<HoldState>& currentHoldStates() const { return active_holds_; }

private:
    std::vector<struct ActiveNote> active_notes_;
    std::vector<HoldState> active_holds_;
    std::vector<JudgeResult> frame_results_;
    Stats stats_;

    // 用于区分"新按下"与"持续按住"，避免持续按住自动判定后续 TAP
    std::unordered_set<int64_t> held_fingers_;

    JudgeType judgeTiming(int64_t delta_ms) const;
    /// SLIDE 专用判定：early 只有 perfect（|delta| <= window_perfect），
    /// late: +window_perfect ~ +window_good 为 GOOD，其余为 MISS
    JudgeType judgeSlideTiming(int64_t delta_ms) const;
    void processHold(int64_t now_ms, const std::vector<struct RawTouch>& touches);
    void processChordMerge(std::vector<struct RawTouch>& touches);
    void projectVerticalJudge(std::vector<struct RawTouch>& touches) const;
    void updateStats(JudgeType type);
    bool isTouchInSide(const RawTouch& touch, SideType side) const;
};

