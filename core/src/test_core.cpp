/**
 * 核心引擎命令行测试程序（无渲染、无音频设备）
 *
 * 测试范围：
 * 1. 谱面解析器（.chart 二进制格式）
 * 2. 判定引擎（模拟触摸序列，验证判定结果）
 * 3. 音频时钟（验证采样-毫秒转换精度）
 * 4. 资源加载安全（路径遍历防护）
 *
 * 使用方法：
 *   g++ -std=c++17 -I src test_core.cpp src/engine/*.cpp src/utils/*.cpp -o test_core
 *   ./test_core ../assets/songs/001/chart_1.chart
 */

#include "engine/audio_clock.h"
#include "engine/judge_engine.h"
#include "engine/chart_parser.h"
#include "engine/input_manager.h"
#include "utils/logger.h"

// asset_loader.cpp 中的自由函数（无对应头文件）
extern bool LoadFile(const std::string& relativePath, std::vector<uint8_t>& outData);
extern std::string GetAssetFullPath(const std::string& relativePath);

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <cmath>

// =============================================================================
// 测试框架（极简）
// =============================================================================
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    printf("[TEST] %-40s ", #name); \
    fflush(stdout); \
    try { test_##name(); printf("PASS\n"); ++g_tests_passed; } \
    catch (const std::exception& e) { printf("FAIL: %s\n", e.what()); ++g_tests_failed; } \
    catch (...) { printf("FAIL: unknown exception\n"); ++g_tests_failed; } \
} while(0)

#define ASSERT_TRUE(cond) do { if (!(cond)) { \
    printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
    throw std::runtime_error("assertion failed"); \
} } while(0)

#define ASSERT_FALSE(cond) do { if (cond) { \
    printf("FAIL (%s:%d): !%s\n", __FILE__, __LINE__, #cond); \
    throw std::runtime_error("assertion failed"); \
} } while(0)

#define ASSERT_EQ(a, b) do { if ((a) != (b)) { \
    printf("FAIL (%s:%d): %s != %s\n", __FILE__, __LINE__, #a, #b); \
    throw std::runtime_error("assertion failed"); \
} } while(0)

#define ASSERT_NEAR(a, b, eps) do { if (std::fabs((a)-(b)) > (eps)) { \
    printf("FAIL (%s:%d): |%s - %s| > %s\n", __FILE__, __LINE__, #a, #b, #eps); \
    throw std::runtime_error("assertion failed"); \
} } while(0)

// =============================================================================
// 测试：音频时钟精度
// =============================================================================
TEST(audio_clock_precision) {
    AudioClock clock;
    clock.init(44100, 2);

    // 验证初始时间为 0
    ASSERT_EQ(clock.nowSamples(), 0);
    ASSERT_EQ(clock.nowMs(), 0.0);

    // 模拟播放 44100 帧（1秒 @ 44100Hz）
    clock.onSamplesPlayed(44100);
    ASSERT_EQ(clock.nowSamples(), 44100);
    ASSERT_NEAR(clock.nowMs(), 1000.0, 0.1); // 1秒 = 1000ms

    // 模拟播放 22050 帧（0.5秒）
    clock.onSamplesPlayed(22050);
    ASSERT_EQ(clock.nowSamples(), 66150);
    ASSERT_NEAR(clock.nowMs(), 1500.0, 0.1);

    // 验证线程安全：多次调用 nowMs() 返回一致结果
    double t1 = clock.nowMs();
    double t2 = clock.nowMs();
    ASSERT_EQ(t1, t2);
}

// =============================================================================
// 测试：判定引擎窗口（通过 public update 接口间接验证）
// =============================================================================
TEST(judge_timing_window) {
    JudgeEngine engine;

    // 构造一个 @ 1000ms 的 TAP note
    std::vector<NoteData> notes;
    notes.push_back({1, NoteType::TAP, 1000, SideType::DOWN, 0.5f, 0.0f, 0, 0, 0, {}});
    engine.loadChart(notes);

    // Perfect 窗口：±59ms，触摸 @ 1059ms（late 59ms）
    {
        std::vector<RawTouch> touches;
        touches.push_back({1, 0.5f, 0.5f, 1059, true, true});
        engine.update(1059, touches);
        auto stats = engine.currentStats();
        ASSERT_EQ(stats.perfect, 1);
    }

    // 重新加载谱面
    engine.loadChart(notes);
    // Good 窗口：±90ms，触摸 @ 1090ms（late 90ms）
    {
        std::vector<RawTouch> touches;
        touches.push_back({1, 0.5f, 0.5f, 1090, true, true});
        engine.update(1090, touches);
        auto stats = engine.currentStats();
        ASSERT_EQ(stats.good, 1);
    }

    // 重新加载谱面
    engine.loadChart(notes);
    // Miss 窗口：>90ms 且 <=150ms 时仍判 Miss，触摸 @ 1150ms（late 150ms）
    {
        std::vector<RawTouch> touches;
        touches.push_back({1, 0.5f, 0.5f, 1150, true, true});
        engine.update(1150, touches);
        auto stats = engine.currentStats();
        ASSERT_EQ(stats.miss, 1);
    }
}

// =============================================================================
// 测试：判定引擎统计计算
// =============================================================================
TEST(judge_stats_calculation) {
    JudgeEngine engine;

    // 构造简单谱面：2个TAP
    std::vector<NoteData> notes;
    notes.push_back({1, NoteType::TAP, 1000, SideType::DOWN, 0.5f, 0.0f, 0, 0, 0, {}});
    notes.push_back({2, NoteType::TAP, 2000, SideType::DOWN, 0.5f, 0.0f, 0, 0, 0, {}});
    engine.loadChart(notes);

    // 模拟两个 Perfect 判定
    std::vector<RawTouch> touches;
    touches.push_back({1, 0.5f, 0.5f, 1000, true, true});
    engine.update(1000, touches);

    touches.clear();
    touches.push_back({2, 0.5f, 0.5f, 2000, true, true});
    engine.update(2000, touches);

    auto stats = engine.currentStats();
    ASSERT_EQ(stats.perfect, 2);
    ASSERT_EQ(stats.good, 0);
    ASSERT_EQ(stats.miss, 0);
    ASSERT_EQ(stats.combo, 2);
    ASSERT_EQ(stats.max_combo, 2);
    ASSERT_TRUE(stats.is_full_combo);
    ASSERT_TRUE(stats.is_all_perfect);
    ASSERT_NEAR(stats.accuracy, 100.0, 0.01);
}

// =============================================================================
// 测试：判定引擎 Miss 自动判定
// =============================================================================
TEST(judge_auto_miss) {
    JudgeEngine engine;

    std::vector<NoteData> notes;
    notes.push_back({1, NoteType::TAP, 1000, SideType::DOWN, 0.5f, 0.0f, 0, 0, 0, {}});
    engine.loadChart(notes);

    // 时间到达 note 时间 + 151ms，超过 Miss 窗口，应自动 Miss
    std::vector<RawTouch> empty;
    engine.update(1151, empty);

    auto stats = engine.currentStats();
    ASSERT_EQ(stats.miss, 1);
    ASSERT_EQ(stats.combo, 0);
    ASSERT_FALSE(stats.is_full_combo);
}

// =============================================================================
// 测试：判定引擎长条(Hold)基本逻辑
// =============================================================================
TEST(judge_hold_basic) {
    JudgeEngine engine;

    std::vector<NoteData> notes;
    // HOLD_HEAD @ 1000ms, 持续时间 500ms -> tail @ 1500ms
    notes.push_back({1, NoteType::HOLD_HEAD, 1000, SideType::DOWN, 0.5f, 0.0f, 500, 0, 0, {}});
    engine.loadChart(notes);

    // 在头判 Perfect 窗口内按下
    std::vector<RawTouch> touches;
    touches.push_back({1, 0.5f, 0.5f, 1000, true, true});
    engine.update(1000, touches);

    auto stats = engine.currentStats();
    ASSERT_EQ(stats.perfect, 1); // 头判 Perfect

    // 继续按住到尾判时刻
    touches.clear();
    touches.push_back({1, 0.5f, 0.5f, 1500, true, false}); // 持续按住
    engine.update(1500, touches);

    stats = engine.currentStats();
    ASSERT_EQ(stats.perfect, 2); // 头判 + 尾判
    ASSERT_EQ(stats.combo, 2);
}

// =============================================================================
// 测试：判定引擎长条断裂
// =============================================================================
TEST(judge_hold_break) {
    JudgeEngine engine;
    engine.hold_break_tolerance_ms = 500; // 默认500ms容错

    std::vector<NoteData> notes;
    notes.push_back({1, NoteType::HOLD_HEAD, 1000, SideType::DOWN, 0.5f, 0.0f, 2000, 0, 0, {}});
    engine.loadChart(notes);

    // 头判 Perfect
    std::vector<RawTouch> touches;
    touches.push_back({1, 0.5f, 0.5f, 1000, true, true});
    engine.update(1000, touches);

    // 松开（无触摸）
    touches.clear();
    engine.update(1200, touches); // 松开200ms，未超过500ms容错

    auto stats = engine.currentStats();
    ASSERT_EQ(stats.perfect, 1); // 只有头判，尾判还未发生

    // 继续松开，超过500ms容错
    engine.update(1600, touches); // 总共松开400ms，仍<500ms
    stats = engine.currentStats();
    ASSERT_EQ(stats.perfect, 1);

    // 超过500ms容错，长条断裂
    engine.update(1700, touches); // 总共松开500ms，刚好触发
    stats = engine.currentStats();
    ASSERT_EQ(stats.miss, 1); // 尾判 Miss
    ASSERT_EQ(stats.perfect, 1); // 头判仍是 Perfect
}

// =============================================================================
// 测试：谱面解析器魔数校验
// =============================================================================
TEST(chart_parser_magic) {
    // 魔数校验在 readChart 中，传入不存在的路径应返回 nullopt
    auto result = ChartParser::readChart("/nonexistent/path/test.chart");
    ASSERT_TRUE(!result.has_value());

    // 空路径也应返回 nullopt
    result = ChartParser::readChart("");
    ASSERT_TRUE(!result.has_value());
}

// =============================================================================
// 测试：谱面解析器加载真实文件
// =============================================================================
TEST(chart_parser_load_real) {
    // 尝试多个可能的路径（相对于不同运行目录）
    const char* paths[] = {
        "../../assets/songs/song_sample/chart_giga.chart",
        "../assets/songs/song_sample/chart_giga.chart",
        "assets/songs/song_sample/chart_giga.chart",
    };
    std::optional<Chart> chart;
    for (const char* p : paths) {
        chart = ChartParser::parseWithCache(p);
        if (chart.has_value()) break;
    }
    ASSERT_TRUE(chart.has_value());

    // 验证解析出的字段
    ASSERT_FALSE(chart->song_id.empty());
    ASSERT_FALSE(chart->difficulty.empty());
    ASSERT_FALSE(chart->map_id.empty());

    // 验证 note 数量合理（应有至少 1 个 note）
    ASSERT_TRUE(chart->note_count > 0);
    ASSERT_EQ(chart->notes.size(), static_cast<size_t>(chart->note_count));

    // 验证 note 时间单调递增（parse 后应已排序）
    for (size_t i = 1; i < chart->notes.size(); ++i) {
        ASSERT_TRUE(chart->notes[i].time_ms >= chart->notes[i-1].time_ms);
    }

    printf("(notes=%zu) ", chart->notes.size());
}

// =============================================================================
// 辅助函数：获取指定侧的标准触摸 x 坐标
// =============================================================================
static float sideCenterX(SideType side) {
    switch (side) {
        case SideType::LEFT:  return 0.15f;
        case SideType::DOWN:  return 0.50f;
        case SideType::RIGHT: return 0.85f;
    }
    return 0.5f;
}

static std::optional<Chart> loadRealChart() {
    std::optional<Chart> chart = ChartParser::parse("assets/songs/song_sample/chart_hard.chart");
    if (!chart.has_value()) {
        chart = ChartParser::parse("../assets/songs/song_sample/chart_hard.chart");
    }
    if (!chart.has_value()) {
        chart = ChartParser::parse("../../assets/songs/song_sample/chart_hard.chart");
    }
    return chart;
}

// =============================================================================
// 测试：真实谱面 TAP Perfect 判定
// =============================================================================
TEST(judge_real_chart_tap_perfect) {
    auto chart = loadRealChart();
    ASSERT_TRUE(chart.has_value());

    // 提取前 20 个 TAP
    std::vector<NoteData> taps;
    for (const auto& n : chart->notes) {
        if (n.type == NoteType::TAP && taps.size() < 20) {
            taps.push_back(n);
        }
    }
    ASSERT_TRUE(taps.size() > 0);

    JudgeEngine engine;
    engine.loadChart(taps);

    // 按时间分组处理，避免同一时间点的双押时间倒流
    std::vector<std::pair<int64_t, std::vector<size_t>>> time_groups;
    for (size_t i = 0; i < taps.size(); ++i) {
        int64_t t = static_cast<int64_t>(taps[i].time_ms);
        if (time_groups.empty() || time_groups.back().first != t) {
            time_groups.push_back({t, {}});
        }
        time_groups.back().second.push_back(i);
    }

    for (const auto& [time_ms, indices] : time_groups) {
        std::vector<RawTouch> touches;
        for (size_t idx : indices) {
            touches.push_back({static_cast<int64_t>(idx + 1), sideCenterX(taps[idx].side), 0.5f,
                               time_ms, true, true});
        }
        engine.update(time_ms, touches);

        // 松开手指
        touches.clear();
        engine.update(time_ms + 1, touches);
    }

    auto stats = engine.currentStats();
    ASSERT_EQ(stats.perfect, static_cast<int>(taps.size()));
    ASSERT_EQ(stats.good, 0);
    ASSERT_EQ(stats.miss, 0);
    ASSERT_TRUE(stats.is_all_perfect);
    printf("(taps=%zu) ", taps.size());
}

// =============================================================================
// 测试：真实谱面 TAP Good 判定（晚 30ms）
// =============================================================================
TEST(judge_real_chart_tap_good) {
    auto chart = loadRealChart();
    ASSERT_TRUE(chart.has_value());

    std::vector<NoteData> taps;
    for (const auto& n : chart->notes) {
        if (n.type == NoteType::TAP && taps.size() < 20) {
            taps.push_back(n);
        }
    }
    ASSERT_TRUE(taps.size() > 0);

    JudgeEngine engine;
    engine.loadChart(taps);

    std::vector<std::pair<int64_t, std::vector<size_t>>> time_groups;
    for (size_t i = 0; i < taps.size(); ++i) {
        int64_t t = static_cast<int64_t>(taps[i].time_ms) + 30;
        if (time_groups.empty() || time_groups.back().first != t) {
            time_groups.push_back({t, {}});
        }
        time_groups.back().second.push_back(i);
    }

    for (const auto& [hit_time, indices] : time_groups) {
        std::vector<RawTouch> touches;
        for (size_t idx : indices) {
            touches.push_back({static_cast<int64_t>(idx + 1), sideCenterX(taps[idx].side), 0.5f,
                               hit_time, true, true});
        }
        engine.update(hit_time, touches);

        touches.clear();
        engine.update(hit_time + 1, touches);
    }

    auto stats = engine.currentStats();
    ASSERT_EQ(stats.perfect, 0);
    ASSERT_EQ(stats.good, static_cast<int>(taps.size()));
    ASSERT_EQ(stats.miss, 0);
    printf("(taps=%zu) ", taps.size());
}

// =============================================================================
// 测试：真实谱面 TAP Miss 自动判定
// =============================================================================
TEST(judge_real_chart_tap_miss) {
    auto chart = loadRealChart();
    ASSERT_TRUE(chart.has_value());

    std::vector<NoteData> taps;
    for (const auto& n : chart->notes) {
        if (n.type == NoteType::TAP && taps.size() < 20) {
            taps.push_back(n);
        }
    }
    ASSERT_TRUE(taps.size() > 0);

    JudgeEngine engine;
    engine.loadChart(taps);

    // 推进到最后一个 note 的 miss 时间之后
    int64_t last_miss_time = 0;
    for (const auto& n : taps) {
        int64_t t = static_cast<int64_t>(n.time_ms) + engine.window_miss + 1;
        if (t > last_miss_time) last_miss_time = t;
    }

    std::vector<RawTouch> empty;
    engine.update(last_miss_time, empty);

    auto stats = engine.currentStats();
    ASSERT_EQ(stats.miss, static_cast<int>(taps.size()));
    ASSERT_EQ(stats.perfect, 0);
    ASSERT_EQ(stats.good, 0);
    printf("(taps=%zu) ", taps.size());
}

// =============================================================================
// 测试：真实谱面 Hold Perfect 判定
// =============================================================================
TEST(judge_real_chart_hold_perfect) {
    auto chart = loadRealChart();
    ASSERT_TRUE(chart.has_value());

    std::vector<NoteData> holds;
    for (const auto& n : chart->notes) {
        if (n.type == NoteType::HOLD_HEAD && n.duration_ms > 0 && holds.size() < 5) {
            holds.push_back(n);
        }
    }
    if (holds.empty()) {
        printf("(no holds) ");
        return;
    }

    JudgeEngine engine;
    engine.loadChart(holds);

    // 计算全局时间范围
    int64_t start_time = static_cast<int64_t>(holds[0].time_ms);
    int64_t end_time = 0;
    for (const auto& n : holds) {
        int64_t tail = static_cast<int64_t>(n.time_ms) + static_cast<int64_t>(n.duration_ms);
        if (tail > end_time) end_time = tail;
    }

    // 按 16ms 帧率模拟，每帧生成当前应按住的所有触摸
    for (int64_t t = start_time; t <= end_time; t += 16) {
        std::vector<RawTouch> touches;
        for (size_t i = 0; i < holds.size(); ++i) {
            int64_t head = static_cast<int64_t>(holds[i].time_ms);
            int64_t tail = head + static_cast<int64_t>(holds[i].duration_ms);
            if (t >= head && t <= tail) {
                touches.push_back({static_cast<int64_t>(i + 100), sideCenterX(holds[i].side), 0.5f,
                                   t, true, (t == head)});
            }
        }
        engine.update(t, touches);
    }

    // 最后一帧后松开
    std::vector<RawTouch> empty;
    engine.update(end_time + 1, empty);

    auto stats = engine.currentStats();
    // 每个 hold：头判 Perfect + 尾判 Perfect
    ASSERT_EQ(stats.perfect, static_cast<int>(holds.size()) * 2);
    ASSERT_EQ(stats.miss, 0);
    ASSERT_EQ(stats.good, 0);
    printf("(holds=%zu) ", holds.size());
}

// =============================================================================
// 测试：真实谱面 Hold 断裂判定
// =============================================================================
TEST(judge_real_chart_hold_break) {
    auto chart = loadRealChart();
    ASSERT_TRUE(chart.has_value());

    std::vector<NoteData> holds;
    for (const auto& n : chart->notes) {
        if (n.type == NoteType::HOLD_HEAD && n.duration_ms > 0 && holds.size() < 5) {
            holds.push_back(n);
        }
    }
    if (holds.empty()) {
        printf("(no holds) ");
        return;
    }

    JudgeEngine engine;
    engine.loadChart(holds);

    for (size_t i = 0; i < holds.size(); ++i) {
        const auto& n = holds[i];
        int64_t fid = static_cast<int64_t>(i + 200);
        float x = sideCenterX(n.side);
        int64_t head_time = static_cast<int64_t>(n.time_ms);

        // 头判
        std::vector<RawTouch> touches;
        touches.push_back({fid, x, 0.5f, head_time, true, true});
        engine.update(head_time, touches);

        // 立即松开
        touches.clear();
        engine.update(head_time + 1, touches);

        // 等待断裂（超过 500ms 容错）
        engine.update(head_time + 502, touches);
    }

    auto stats = engine.currentStats();
    // 头判 Perfect + 尾判 Miss
    ASSERT_EQ(stats.perfect, static_cast<int>(holds.size()));
    ASSERT_EQ(stats.miss, static_cast<int>(holds.size()));
    ASSERT_EQ(stats.good, 0);
    printf("(holds=%zu) ", holds.size());
}

// =============================================================================
// 测试：多押糊谱惩罚机制
// =============================================================================
TEST(judge_multi_finger_spam_penalty) {
    JudgeEngine engine;

    // 构造 3 个不同时的 TAP：1000ms, 1050ms, 1100ms（间隔 50ms，都在 miss 窗口内）
    std::vector<NoteData> notes;
    notes.push_back({1, NoteType::TAP, 1000, SideType::DOWN, 0.5f, 0.0f, 0, 0, 0, {}});
    notes.push_back({2, NoteType::TAP, 1050, SideType::DOWN, 0.5f, 0.0f, 0, 0, 0, {}});
    notes.push_back({3, NoteType::TAP, 1100, SideType::DOWN, 0.5f, 0.0f, 0, 0, 0, {}});
    engine.loadChart(notes);

    // 3 个手指同时在 1000ms 按下，试图糊谱
    std::vector<RawTouch> touches;
    touches.push_back({1, 0.5f, 0.5f, 1000, true, true});
    touches.push_back({2, 0.5f, 0.5f, 1000, true, true});
    touches.push_back({3, 0.5f, 0.5f, 1000, true, true});
    engine.update(1000, touches);

    auto stats = engine.currentStats();
    // 糊谱惩罚：3 个不同时的 note 同时被判定 → 全部 Miss
    ASSERT_EQ(stats.miss, 3);
    ASSERT_EQ(stats.perfect, 0);
    ASSERT_EQ(stats.good, 0);
}

// =============================================================================
// 测试：双押不同时触控 → 正常判定（非糊谱）
// =============================================================================
TEST(judge_chord_different_touch_times) {
    JudgeEngine engine;

    // 两个同时的 TAP @ 1000ms（双押）
    std::vector<NoteData> notes;
    notes.push_back({1, NoteType::TAP, 1000, SideType::LEFT,  0.15f, 0.0f, 0, 0, 0, {}});
    notes.push_back({2, NoteType::TAP, 1000, SideType::RIGHT, 0.85f, 0.0f, 0, 0, 0, {}});
    engine.loadChart(notes);

    // 手指1 在 980ms（提前 20ms）按下，手指2 在 1020ms（延后 20ms）按下
    std::vector<RawTouch> touches1;
    touches1.push_back({1, 0.15f, 0.5f, 980, true, true});
    engine.update(980, touches1);

    std::vector<RawTouch> touches2;
    touches2.push_back({2, 0.85f, 0.5f, 1020, true, true});
    engine.update(1020, touches2);

    auto stats = engine.currentStats();
    // 双押合法：不同时的触控判定同时的 note → 正常 Perfect
    ASSERT_EQ(stats.perfect, 2);
    ASSERT_EQ(stats.miss, 0);
}

// =============================================================================
// 测试：XML 谱面 BPM 变速解析（GIGA.xml — 含 BPM change）
// =============================================================================
TEST(chart_parser_xml_bpm_change) {
    const char* paths[] = {
        "../../GIGA.xml",
        "../GIGA.xml",
        "GIGA.xml",
    };
    std::optional<Chart> chart;
    for (const char* p : paths) {
        chart = ChartParser::parseXmlFile(p);
        if (chart.has_value()) break;
    }
    ASSERT_TRUE(chart.has_value());

    // m_barPerMin=48 → 实际 BPM = 48×4 = 192
    ASSERT_NEAR(chart->bpm, 192.0f, 0.01f);
    // m_timeOffset=0.664
    ASSERT_NEAR(chart->offset_sec, 0.664, 0.001);
    // m_mapID=_map_И00._G
    ASSERT_EQ(chart->difficulty, "GIGA");
    ASSERT_FALSE(chart->map_id.empty());
    ASSERT_FALSE(chart->song_id.empty());
    // bpm_events: 1 entry at beat 0
    ASSERT_TRUE(chart->bpm_events.size() >= 1);
    ASSERT_NEAR(chart->bpm_events[0].bpm, 192.0f, 0.01f);

    printf("(bpm=%.1f, events=%zu, notes=%zu) ",
           chart->bpm, chart->bpm_events.size(), chart->notes.size());
}

// =============================================================================
// 测试：XML 谱面无变速（floating city.xml — 无 BPM change）
// =============================================================================
TEST(chart_parser_xml_no_bpm_change) {
    const char* paths[] = {
        "../../floating city.xml",
        "../floating city.xml",
        "floating city.xml",
    };
    std::optional<Chart> chart;
    for (const char* p : paths) {
        chart = ChartParser::parseXmlFile(p);
        if (chart.has_value()) break;
    }
    ASSERT_TRUE(chart.has_value());

    // m_barPerMin=31.25 → 实际 BPM = 31.25×4 = 125
    ASSERT_NEAR(chart->bpm, 125.0f, 0.01f);
    // m_timeOffset=-15.920833
    ASSERT_TRUE(chart->offset_sec < 0.0);
    // 无变速
    ASSERT_EQ(chart->bpm_events.size(), 0u);
    ASSERT_EQ(chart->difficulty, "GIGA");
    ASSERT_FALSE(chart->song_id.empty());

    printf("(bpm=%.1f, offset=%.3f, notes=%zu) ",
           chart->bpm, chart->offset_sec, chart->notes.size());
}

// =============================================================================
// 测试：ZIP 包解析
// =============================================================================
TEST(chart_parser_zip_package) {
    const char* paths[] = {
        "../../cankao/[H10 G15]RosenkreuzVampir.zip",
        "../cankao/[H10 G15]RosenkreuzVampir.zip",
        "cankao/[H10 G15]RosenkreuzVampir.zip",
    };
    std::optional<std::vector<Chart>> charts;
    for (const char* p : paths) {
        charts = ChartParser::parsePackage(p);
        if (charts.has_value()) break;
    }
    ASSERT_TRUE(charts.has_value());
    ASSERT_TRUE(charts->size() >= 1);

    // 验证每个谱面字段
    for (const auto& c : *charts) {
        ASSERT_FALSE(c.difficulty.empty());
        ASSERT_NEAR(c.bpm, 192.0f, 0.01f);  // 48×4
        ASSERT_TRUE(c.note_count > 0);
        ASSERT_TRUE(c.duration_ms > 0);
    }

    // ZIP 包 parsePackageInfo
    const char* zip_path = nullptr;
    for (const char* p : paths) {
        std::ifstream test_f(p);
        if (test_f.is_open()) { zip_path = p; break; }
    }
    if (zip_path) {
        auto pkg = ChartParser::parsePackageInfo(zip_path);
        ASSERT_TRUE(pkg.has_value());
        ASSERT_FALSE(pkg->music_name.empty());
        printf("(charts=%zu '%s' by %s) ",
               charts->size(), pkg->music_name.c_str(), pkg->noter_name.c_str());
    } else {
        printf("(charts=%zu) ", charts->size());
    }
}

// =============================================================================
// 测试：ZIP 包 per-difficulty 缓存路径
// =============================================================================
TEST(chart_parser_cache_path_diff) {
    // 无 difficulty → 基础路径
    std::string base = ChartParser::cachePathFrom("song.zip");
    ASSERT_EQ(base, "song.chart");

    // 带 difficulty → song_GIGA.chart
    std::string diff = ChartParser::cachePathFrom("song.zip", "GIGA");
    ASSERT_EQ(diff, "song_GIGA.chart");

    // 对 XML 也一样
    std::string xml_cache = ChartParser::cachePathFrom("chart.xml", "HARD");
    ASSERT_EQ(xml_cache, "chart_HARD.chart");
}

// =============================================================================
// 测试：资源加载路径安全
// =============================================================================
TEST(asset_loader_path_security) {
    // 绝对路径应被拒绝
    ASSERT_TRUE(GetAssetFullPath("/etc/passwd").empty());

    // 路径遍历应被拒绝
    ASSERT_TRUE(GetAssetFullPath("../secret.txt").empty());
    ASSERT_TRUE(GetAssetFullPath("foo/../../secret.txt").empty());

    // 非法扩展名应被拒绝
    ASSERT_TRUE(GetAssetFullPath("config.exe").empty());
    ASSERT_TRUE(GetAssetFullPath("data.dll").empty());

    // 合法路径应通过
    ASSERT_TRUE(!GetAssetFullPath("songs/001/cover.png").empty());
    ASSERT_TRUE(!GetAssetFullPath("songs/001/bgm.ogg").empty());
    ASSERT_TRUE(!GetAssetFullPath("config/default_settings.json").empty());
}

// =============================================================================
// 主入口
// =============================================================================
int main(int argc, char** argv) {
    (void)argc; (void)argv;

    printf("========================================\n");
    printf("Dynamite Core Engine Test Suite\n");
    printf("========================================\n\n");

    RUN_TEST(audio_clock_precision);
    RUN_TEST(judge_timing_window);
    RUN_TEST(judge_stats_calculation);
    RUN_TEST(judge_auto_miss);
    RUN_TEST(judge_hold_basic);
    RUN_TEST(judge_hold_break);
    RUN_TEST(chart_parser_magic);
    RUN_TEST(chart_parser_load_real);
    RUN_TEST(judge_real_chart_tap_perfect);
    RUN_TEST(judge_real_chart_tap_good);
    RUN_TEST(judge_real_chart_tap_miss);
    RUN_TEST(judge_real_chart_hold_perfect);
    RUN_TEST(judge_real_chart_hold_break);
    RUN_TEST(judge_multi_finger_spam_penalty);
    RUN_TEST(judge_chord_different_touch_times);
    RUN_TEST(chart_parser_xml_bpm_change);
    RUN_TEST(chart_parser_xml_no_bpm_change);
    RUN_TEST(chart_parser_zip_package);
    RUN_TEST(chart_parser_cache_path_diff);
    RUN_TEST(asset_loader_path_security);

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    printf("========================================\n");

    return g_tests_failed > 0 ? 1 : 0;
}
