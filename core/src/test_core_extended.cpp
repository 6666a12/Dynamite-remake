/**
 * Dynamite Core Engine — 扩展单元测试
 * 覆盖 audio_clock, judge_engine, chart_parser 的边界情况和未测路径
 */
#include <catch2/catch_amalgamated.hpp>

#include "engine/audio_clock.h"
#include "engine/judge_engine.h"
#include "engine/chart_parser.h"
#include "engine/input_manager.h"

#include <cmath>
#include <vector>
#include <cstdint>
#include <fstream>

// ============================================================================
// AudioClock 边界测试
// ============================================================================

TEST_CASE("AudioClock: 不同采样率") {
    AudioClock clock;
    
    SECTION("48kHz") {
        clock.init(48000, 2);
        clock.onSamplesPlayed(48000);
        REQUIRE(std::abs(clock.nowMs() - 1000.0) <= 0.1);
    }
    
    SECTION("96kHz") {
        clock.init(96000, 2);
        clock.onSamplesPlayed(96000);
        REQUIRE(std::abs(clock.nowMs() - 1000.0) <= 0.1);
    }
    
    SECTION("22.05kHz") {
        clock.init(22050, 1);
        clock.onSamplesPlayed(22050);
        REQUIRE(std::abs(clock.nowMs() - 1000.0) <= 0.1);
    }
}

TEST_CASE("AudioClock: 累计播放") {
    AudioClock clock;
    clock.init(44100, 2);
    
    clock.onSamplesPlayed(44100);  // 1秒
    REQUIRE(clock.nowSamples() == 44100);
    
    clock.onSamplesPlayed(22050);  // 再0.5秒
    REQUIRE(clock.nowSamples() == 66150);
    
    clock.onSamplesPlayed(44100);  // 再1秒
    REQUIRE(clock.nowSamples() == 110250);
    REQUIRE(std::abs(clock.nowMs() - 2500.0) <= 0.1);
}

TEST_CASE("AudioClock: 零采样") {
    AudioClock clock;
    clock.init(44100, 2);
    
    clock.onSamplesPlayed(0);
    REQUIRE(clock.nowSamples() == 0);
    REQUIRE(clock.nowMs() == 0.0);
}

TEST_CASE("AudioClock: 大数值") {
    AudioClock clock;
    clock.init(192000, 2);
    
    // 10分钟 @ 192kHz
    uint64_t samples = 192000ULL * 600;
    clock.onSamplesPlayed(samples);
    REQUIRE(std::abs(clock.nowMs() - 600000.0) <= 1.0);
}

// ============================================================================
// JudgeEngine 扩展测试
// ============================================================================

static NoteData makeNote(int id, NoteType type, int64_t time_ms, 
                                       SideType side, uint64_t dur = 0) {
    NoteData nd{};
    nd.id = static_cast<uint32_t>(id);
    nd.type = type;
    nd.time_sec = static_cast<double>(time_ms) / 1000.0;
    nd.time_ms = static_cast<uint64_t>(time_ms);
    nd.side = side;
    nd.position = 0.5f;
    nd.width = 0.0f;
    nd.duration_ms = dur;
    nd.flags = 0;
    nd.sub_id = 0;
    nd.next_id = 0;
    return nd;
}

static RawTouch makeTouch(int64_t fid, float x, float y, int64_t ts, bool down = true) {
    RawTouch t{};
    t.finger_id = fid;
    t.x = x; t.y = y;
    t.timestamp_ms = ts;
    t.is_down = down;
    t.is_new = true;
    t.can_project = true;
    return t;
}

TEST_CASE("JudgeEngine: 连击追踪") {
    JudgeEngine engine;
    std::vector<NoteData> notes;
    notes.push_back(makeNote(1, NoteType::TAP, 1000, SideType::DOWN));
    notes.push_back(makeNote(2, NoteType::TAP, 2000, SideType::DOWN));
    notes.push_back(makeNote(3, NoteType::TAP, 3000, SideType::DOWN));
    engine.loadChart(notes);
    
    // Note 1: PERFECT
    engine.update(1000, {makeTouch(1, 0.5f, 0.5f, 1000)});
    auto s = engine.currentStats();
    REQUIRE(s.perfect == 1);
    REQUIRE(s.combo == 1);
    REQUIRE(s.max_combo == 1);
    
    // Note 2: PERFECT
    engine.update(2000, {makeTouch(2, 0.5f, 0.5f, 2000)});
    s = engine.currentStats();
    REQUIRE(s.perfect == 2);
    REQUIRE(s.combo == 2);
    REQUIRE(s.max_combo == 2);
    
    // Note 3: auto-miss (no touch)
    engine.update(3151, {});
    s = engine.currentStats();
    REQUIRE(s.miss == 1);
    REQUIRE(s.combo == 0);
    REQUIRE(s.max_combo == 2);
}

TEST_CASE("JudgeEngine: 准确率计算") {
    JudgeEngine engine;
    std::vector<NoteData> notes;
    notes.push_back(makeNote(1, NoteType::TAP, 1000, SideType::DOWN));
    notes.push_back(makeNote(2, NoteType::TAP, 2000, SideType::DOWN));
    notes.push_back(makeNote(3, NoteType::TAP, 3000, SideType::DOWN));
    notes.push_back(makeNote(4, NoteType::TAP, 4000, SideType::DOWN));
    engine.loadChart(notes);
    
    // 2 Perfect + 1 Good + 1 Miss
    engine.update(1000, {makeTouch(1, 0.5f, 0.5f, 1000)});     // Perfect
    engine.update(2070, {makeTouch(2, 0.5f, 0.5f, 2070)});     // late 70ms → Good
    engine.update(3151, {});                                     // auto-miss
    
    auto s = engine.currentStats();
    REQUIRE(s.perfect == 1);
    REQUIRE(s.good == 1);
    REQUIRE(s.miss == 1);
    // Accuracy: (1*100 + 1*50 + 1*0) / 3 = 50%
    REQUIRE(std::abs(s.accuracy - 50.0) < 1.0);
    
    // Note 4: Perfect
    engine.update(4000, {makeTouch(4, 0.5f, 0.5f, 4000)});
    s = engine.currentStats();
    // Accuracy: (2*100 + 1*50 + 1*0) / 4 = 62.5%
    REQUIRE(std::abs(s.accuracy - 62.5) < 1.0);
}

TEST_CASE("JudgeEngine: 多条 Hold 同时") {
    JudgeEngine engine;
    std::vector<NoteData> notes;
    notes.push_back(makeNote(1, NoteType::HOLD_HEAD, 1000, SideType::DOWN, 500));
    notes.push_back(makeNote(2, NoteType::HOLD_HEAD, 1000, SideType::LEFT,  500));
    notes.push_back(makeNote(3, NoteType::HOLD_HEAD, 1000, SideType::RIGHT, 500));
    engine.loadChart(notes);
    
    // 三个 Hold 同时按下（头判）
    std::vector<RawTouch> touches = {
        makeTouch(1, 0.50f, 0.5f, 1000),    // DOWN
        makeTouch(2, 0.05625f, 0.5f, 1000),  // LEFT
        makeTouch(3, 0.94375f, 0.5f, 1000),  // RIGHT
    };
    engine.update(1000, touches);
    auto s = engine.currentStats();
    REQUIRE(s.perfect == 3);
    REQUIRE(s.combo == 3);
    
    // 持续按住到尾判
    engine.update(1500, touches);
    s = engine.currentStats();
    REQUIRE(s.perfect == 6);  // 3 head + 3 tail
    REQUIRE(s.combo == 6);
}

TEST_CASE("JudgeEngine: MULTI 类型处理") {
    JudgeEngine engine;
    std::vector<NoteData> notes;
    notes.push_back(makeNote(1, NoteType::MULTI, 1000, SideType::DOWN));
    engine.loadChart(notes);
    
    engine.update(1000, {makeTouch(1, 0.5f, 0.5f, 1000)});
    auto s = engine.currentStats();
    REQUIRE(s.perfect == 1);
}

TEST_CASE("JudgeEngine: HOLD_BODY 被跳过") {
    JudgeEngine engine;
    std::vector<NoteData> notes;
    notes.push_back(makeNote(1, NoteType::HOLD_HEAD, 1000, SideType::DOWN, 500));
    notes.push_back(makeNote(2, NoteType::HOLD_BODY, 1200, SideType::DOWN));
    notes.push_back(makeNote(3, NoteType::HOLD_TAIL, 1500, SideType::DOWN));
    engine.loadChart(notes);
    
    // HOLD_BODY should be skipped in loadChart and update
    engine.update(1000, {makeTouch(1, 0.5f, 0.5f, 1000)});
    engine.update(1500, {makeTouch(1, 0.5f, 0.5f, 1500)});
    
    auto s = engine.currentStats();
    REQUIRE(s.perfect == 2);  // head + tail only
    REQUIRE(s.miss == 0);
}

TEST_CASE("JudgeEngine: 边界判定窗口") {
    JudgeEngine engine;
    std::vector<NoteData> notes;
    notes.push_back(makeNote(1, NoteType::TAP, 1000, SideType::DOWN));
    engine.loadChart(notes);
    
    SECTION("Perfect 边界 +59ms") {
        engine.update(1059, {makeTouch(1, 0.5f, 0.5f, 1059)});
        REQUIRE(engine.currentStats().perfect == 1);
    }
    
    SECTION("Good 边界 +60ms") {
        engine.loadChart(notes);
        engine.update(1060, {makeTouch(1, 0.5f, 0.5f, 1060)});
        REQUIRE(engine.currentStats().good == 1);
    }
    
    SECTION("Good 边界 +90ms") {
        engine.loadChart(notes);
        engine.update(1090, {makeTouch(1, 0.5f, 0.5f, 1090)});
        REQUIRE(engine.currentStats().good == 1);
    }
    
    SECTION("超出窗口 +91ms") {
        engine.loadChart(notes);
        engine.update(1091, {makeTouch(1, 0.5f, 0.5f, 1091)});
        // Touch outside window; note auto-misses at 1150
        engine.update(1151, {});
        REQUIRE(engine.currentStats().miss == 1);
    }
}

// ============================================================================
// ChartParser 二进制格式测试
// ============================================================================

TEST_CASE("ChartParser: 魔数验证") {
    REQUIRE(ChartParser::kMagic == 0x544E5944);  // 'DYNT'
    REQUIRE(ChartParser::kVersion == 3);
}

TEST_CASE("ChartParser: 缓存路径推断") {
    SECTION("XML → .chart") {
        auto path = ChartParser::cachePathFrom("songs/test/map.xml");
        REQUIRE(path == "songs/test/map.chart");
    }
    
    SECTION("JSON → .chart") {
        auto path = ChartParser::cachePathFrom("data/chart.json");
        REQUIRE(path == "data/chart.chart");
    }
    
    SECTION("ZIP with difficulty") {
        auto path = ChartParser::cachePathFrom("pack.zip", "GIGA");
        REQUIRE(path == "pack_GIGA.chart");
    }
    
    SECTION("Absolute path") {
        auto path = ChartParser::cachePathFrom("/home/user/map.xml");
        REQUIRE(path == "/home/user/map.chart");
    }
    
    SECTION("No extension") {
        auto path = ChartParser::cachePathFrom("raw_file");
        REQUIRE(path == "raw_file.chart");
    }
}

TEST_CASE("ChartParser: 空图表验证") {
    Chart chart;
    chart.song_id = "test";
    chart.difficulty = "EASY";
    chart.map_id = "test_easy";
    chart.bpm = 120.0f;
    chart.offset_sec = 0.0;
    chart.note_count = 0;
    chart.duration_ms = 0;
    chart.chart_constant = 1.0f;
    
    REQUIRE(chart.song_id == "test");
    REQUIRE(chart.notes.empty());
    REQUIRE(chart.bpm_events.empty());
}

TEST_CASE("ChartParser: 写后读一致性") {
    Chart original;
    original.song_id = "roundtrip_test";
    original.difficulty = "HARD";
    original.map_id = "roundtrip_hard";
    original.bpm = 150.0f;
    original.offset_sec = -0.05;
    original.note_count = 3;
    original.duration_ms = 60000;
    original.chart_constant = 12.5f;
    
    // 添加测试音符
    NoteData n1{};
    n1.id = 1;
    n1.type = NoteType::TAP;
    n1.time_sec = 1.0;
    n1.time_ms = 1000;
    n1.side = SideType::DOWN;
    n1.position = 0.5f;
    n1.width = 1.0f;
    n1.sub_id = 0;
    n1.duration_ms = 0;
    n1.flags = 0;
    n1.next_id = 0;
    original.notes.push_back(n1);
    
    NoteData n2{};
    n2.id = 2;
    n2.type = NoteType::HOLD_HEAD;
    n2.time_sec = 2.0;
    n2.time_ms = 2000;
    n2.side = SideType::LEFT;
    n2.position = 0.2f;
    n2.width = 0.5f;
    n2.sub_id = 3;
    n2.duration_ms = 1000;
    n2.flags = 1;
    n2.next_id = 0;
    original.notes.push_back(n2);
    
    NoteData n3{};
    n3.id = 3;
    n3.type = NoteType::HOLD_TAIL;
    n3.time_sec = 3.0;
    n3.time_ms = 3000;
    n3.side = SideType::LEFT;
    n3.position = 0.2f;
    n3.width = 0.5f;
    n3.sub_id = 0;
    n3.duration_ms = 0;
    n3.flags = 0;
    n3.next_id = 0;
    original.notes.push_back(n3);
    
    // Add BPM change
    BPMEvent bpm;
    bpm.time_sec = 5.0;
    bpm.time_ms = 5000;
    bpm.bpm = 180.0f;
    bpm.beat_num = 4;
    bpm.beat_den = 4;
    original.bpm_events.push_back(bpm);
    
    // Write
    std::string temp_path = "temp_roundtrip.chart";
    REQUIRE(ChartParser::writeChart(original, temp_path));
    
    // Read back
    auto loaded = ChartParser::readChart(temp_path);
    REQUIRE(loaded.has_value());
    
    // Verify
    REQUIRE(loaded->song_id == original.song_id);
    REQUIRE(loaded->difficulty == original.difficulty);
    REQUIRE(loaded->map_id == original.map_id);
    REQUIRE(loaded->bpm == original.bpm);
    REQUIRE(std::abs(loaded->offset_sec - original.offset_sec) < 0.001);
    REQUIRE(loaded->note_count == original.note_count);
    REQUIRE(loaded->duration_ms == original.duration_ms);
    REQUIRE(std::abs(loaded->chart_constant - original.chart_constant) < 0.01f);
    REQUIRE(loaded->notes.size() == original.notes.size());
    REQUIRE(loaded->bpm_events.size() == original.bpm_events.size());
    
    // Verify first note
    REQUIRE(loaded->notes[0].id == n1.id);
    REQUIRE(loaded->notes[0].type == n1.type);
    REQUIRE(loaded->notes[0].time_ms == n1.time_ms);
    REQUIRE(loaded->notes[0].side == n1.side);
    
    // Verify BPM event
    REQUIRE(loaded->bpm_events[0].time_ms == bpm.time_ms);
    REQUIRE(loaded->bpm_events[0].bpm == bpm.bpm);
    
    // Cleanup
    std::remove(temp_path.c_str());
}

TEST_CASE("ChartParser: 不存在文件返回 nullopt") {
    auto chart = ChartParser::readChart("nonexistent_file_12345.chart");
    REQUIRE_FALSE(chart.has_value());
}

TEST_CASE("ChartParser: 无效魔数返回 nullopt") {
    // Create a file with invalid magic
    std::ofstream f("temp_bad.chart", std::ios::binary);
    uint32_t bad_magic = 0x12345678;
    f.write(reinterpret_cast<const char*>(&bad_magic), 4);
    f.close();
    
    auto chart = ChartParser::readChart("temp_bad.chart");
    REQUIRE_FALSE(chart.has_value());
    
    std::remove("temp_bad.chart");
}
