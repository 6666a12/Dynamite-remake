/**
 * @file chart_parser.cpp
 * @brief 谱面解析器实现 —— 原生支持 DynaMaker XML/ZIP/JSON 格式
 */

#include "engine/chart_parser.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <regex>

double ChartParser::beatToSec(double beat_time, float bpm, double offset) {
    if (bpm <= 0.0f) return 0.0;
    return beat_time * (60.0 / static_cast<double>(bpm)) - offset;
}

bool ChartParser::validateBounds(const Chart& chart) {
    for (const auto& note : chart.notes) {
        const uint8_t side_val = static_cast<uint8_t>(note.side);
        if (side_val > 2) return false;
    }
    return true;
}

// ============================================================
// XML 解析
// ============================================================

std::optional<Chart> ChartParser::parseXmlString(const std::string& xml_str) {
    Chart chart{};

    std::smatch m;
    if (std::regex_search(xml_str, m, std::regex(R"(<m_barPerMin>([^<]+)</m_barPerMin>)")))
        chart.bpm = std::stof(m[1].str());
    if (std::regex_search(xml_str, m, std::regex(R"(<m_timeOffset>([^<]+)</m_timeOffset>)")))
        chart.offset_sec = std::stod(m[1].str());
    if (std::regex_search(xml_str, m, std::regex(R"(<m_leftRegion>([^<]+)</m_leftRegion>)")))
        chart.left_region = m[1].str();
    if (std::regex_search(xml_str, m, std::regex(R"(<m_rightRegion>([^<]+)</m_rightRegion>)")))
        chart.right_region = m[1].str();
    if (std::regex_search(xml_str, m, std::regex(R"(<m_mapID>([^<]+)</m_mapID>)")))
        chart.map_id = m[1].str();

    // 从 mapID 解析难度
    if (!chart.map_id.empty()) {
        char dc = chart.map_id.back();
        if (dc == 'C') chart.difficulty = "CASUAL";
        else if (dc == 'N') chart.difficulty = "NORMAL";
        else if (dc == 'H') chart.difficulty = "HARD";
        else if (dc == 'M') chart.difficulty = "MEGA";
        else if (dc == 'G') chart.difficulty = "GIGA";
        else if (dc == 'T') chart.difficulty = "TERA";
        else chart.difficulty = "UNKNOWN";
    }

    // 解析轨道
    auto parseNoteSection = [&](const std::string& sec, SideType side) {
        std::regex sec_re("<" + sec + R"(>.*?</)" + sec + ">)",
                         std::regex::icase | std::regex::dotall);
        std::smatch sm_sec;
        if (!std::regex_search(xml_str, sm_sec, sec_re)) return;
        std::string sec_content = sm_sec[0].str();

        std::regex note_re(R"(<CMapNoteAsset>(.*?)</CMapNoteAsset>)",
                          std::regex::icase | std::regex::dotall);
        auto b = std::sregex_iterator(sec_content.begin(), sec_content.end(), note_re);
        auto e = std::sregex_iterator();

        for (auto it = b; it != e; ++it) {
            std::string nx = (*it)[1].str();
            NoteData note{};
            note.side = side;
            std::smatch sm;

            if (std::regex_search(nx, sm, std::regex(R"(<m_id>([^<]+)</m_id>)")))
                note.id = (uint32_t)std::stoul(sm[1].str());
            if (std::regex_search(nx, sm, std::regex(R"(<m_type>([^<]+)</m_type>)"))) {
                std::string t = sm[1].str();
                if (t == "NORMAL") note.type = NoteType::TAP;
                else if (t == "HOLD") note.type = NoteType::HOLD_HEAD;
                else if (t == "SUB") note.type = NoteType::HOLD_TAIL;
                else if (t == "CHAIN") note.type = NoteType::SLIDE;
            }
            if (std::regex_search(nx, sm, std::regex(R"(<m_time>([^<]+)</m_time>)"))) {
                double bt = std::stod(sm[1].str());
                note.time_sec = beatToSec(bt, chart.bpm, chart.offset_sec);
                note.time_ms = (uint64_t)(note.time_sec * 1000.0);
            }
            if (std::regex_search(nx, sm, std::regex(R"(<m_position>([^<]+)</m_position>)")))
                note.position = std::stof(sm[1].str());
            if (std::regex_search(nx, sm, std::regex(R"(<m_width>([^<]+)</m_width>)")))
                note.width = std::stof(sm[1].str());
            if (std::regex_search(nx, sm, std::regex(R"(<m_subId>([^<]+)</m_subId>)")))
                note.sub_id = (uint32_t)std::stol(sm[1].str());

            chart.notes.push_back(note);
        }
    };

    parseNoteSection("m_notes", SideType::DOWN);
    parseNoteSection("m_notesLeft", SideType::LEFT);
    parseNoteSection("m_notesRight", SideType::RIGHT);

    // 计算 HOLD duration：通过 sub_id 找到对应的 SUB 计算出时长
    for (auto& n : chart.notes) {
        if (n.type == NoteType::HOLD_HEAD && n.sub_id != UINT32_MAX) {
            for (const auto& sub : chart.notes) {
                if (sub.type == NoteType::HOLD_TAIL && sub.id == n.sub_id && sub.time_ms >= n.time_ms) {
                    n.duration_ms = sub.time_ms - n.time_ms;
                    break;
                }
            }
        }
    }

    chart.note_count = (int64_t)chart.notes.size();
    chart.duration_ms = 0;
    for (auto& n : chart.notes) {
        uint64_t end = n.time_ms + n.duration_ms;
        if (end > chart.duration_ms) chart.duration_ms = end;
    }

    return chart;
}

std::optional<Chart> ChartParser::parseXmlFile(const std::string& file_path) {
    std::ifstream f(file_path);
    if (!f.is_open()) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    return parseXmlString(ss.str());
}

// ============================================================
// ZIP 包解析（占位，后续实现）
// ============================================================

std::optional<DynaMakerPackage> ChartParser::parsePackageInfo(const std::string& zip_path) {
    (void)zip_path;
    return std::nullopt;
}

std::optional<std::vector<Chart>> ChartParser::parsePackage(const std::string& zip_path) {
    (void)zip_path;
    return std::nullopt;
}

// ============================================================
// JSON 解析
// ============================================================

std::optional<Chart> ChartParser::parseDynaMakerJson(const std::string& json_str) {
    try {
        auto j = nlohmann::json::parse(json_str);
        Chart chart{};
        chart.bpm = 60.0f;
        uint32_t next_id = 0;

        auto parseType = [](const std::string& ts) -> NoteType {
            std::string u = ts;
            std::transform(u.begin(), u.end(), u.begin(), ::toupper);
            if (u == "TAP") return NoteType::TAP;
            if (u == "HOLD") return NoteType::HOLD_HEAD;
            if (u == "SUB") return NoteType::HOLD_TAIL;
            if (u == "SLIDE") return NoteType::SLIDE;
            return NoteType::TAP;
        };

        auto parseArray = [&](const char* key, SideType side, NoteType def) {
            if (!j.contains(key) || !j[key].is_array()) return;
            for (auto& elem : j[key]) {
                if (!elem.is_object()) continue;
                NoteData note{};
                note.id = next_id++;
                note.side = side;

                if (elem.contains("time") && elem["time"].is_number())
                    note.time_ms = (uint64_t)elem["time"].get<double>();
                else if (elem.contains("m_time") && elem["m_time"].is_number())
                    note.time_ms = (uint64_t)(elem["m_time"].get<double>() * 1000.0);
                note.time_sec = note.time_ms / 1000.0;

                if (elem.contains("type") && elem["type"].is_string())
                    note.type = parseType(elem["type"].get<std::string>());
                else if (elem.contains("m_type") && elem["m_type"].is_string())
                    note.type = parseType(elem["m_type"].get<std::string>());
                else
                    note.type = def;

                if (elem.contains("duration") && elem["duration"].is_number())
                    note.duration_ms = elem["duration"].get<uint64_t>();
                else if (elem.contains("holdTime") && elem["holdTime"].is_number())
                    note.duration_ms = elem["holdTime"].get<uint64_t>();

                if (note.duration_ms > 0 && note.type != NoteType::HOLD_TAIL)
                    note.type = NoteType::HOLD_HEAD;

                if (elem.contains("position") && elem["position"].is_number())
                    note.position = elem["position"].get<float>();
                else if (elem.contains("m_position") && elem["m_position"].is_number())
                    note.position = elem["m_position"].get<float>();

                if (elem.contains("width") && elem["width"].is_number())
                    note.width = elem["width"].get<float>();
                else if (elem.contains("m_width") && elem["m_width"].is_number())
                    note.width = elem["m_width"].get<float>();

                chart.notes.push_back(note);
            }
        };

        parseArray("left", SideType::LEFT, NoteType::TAP);
        parseArray("down", SideType::DOWN, NoteType::TAP);
        parseArray("right", SideType::RIGHT, NoteType::TAP);
        parseArray("slide", SideType::DOWN, NoteType::SLIDE);

        chart.note_count = (int64_t)chart.notes.size();
        chart.duration_ms = 0;
        for (auto& n : chart.notes) {
            uint64_t end = n.time_ms + n.duration_ms;
            if (end > chart.duration_ms) chart.duration_ms = end;
        }

        return chart;
    } catch (...) {
        return std::nullopt;
    }
}

// ============================================================
// 统一入口
// ============================================================

std::optional<Chart> ChartParser::parse(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return std::nullopt;
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".xml") return parseXmlFile(path);
    if (ext == ".json") {
        std::ifstream f(path);
        if (!f.is_open()) return std::nullopt;
        std::stringstream ss;
        ss << f.rdbuf();
        return parseDynaMakerJson(ss.str());
    }
    if (ext == ".zip") {
        auto charts = parsePackage(path);
        if (charts && !charts->empty()) return charts->front();
        return std::nullopt;
    }
    return std::nullopt;
return std::nullopt;
}

// ============================================================
// .chart binary serialization / deserialization
// ============================================================

#include <fstream>
#include <cstring>

static void writeU32(std::ofstream& os, uint32_t v) {
    os.put(static_cast<char>(v & 0xFF));
    os.put(static_cast<char>((v >> 8) & 0xFF));
    os.put(static_cast<char>((v >> 16) & 0xFF));
    os.put(static_cast<char>((v >> 24) & 0xFF));
}
static void writeU64(std::ofstream& os, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        os.put(static_cast<char>(v & 0xFF));
        v >>= 8;
    }
}
static void writeF32(std::ofstream& os, float v) {
    uint32_t bits; std::memcpy(&bits, &v, sizeof(bits)); writeU32(os, bits);
}
static void writeF64(std::ofstream& os, double v) {
    uint64_t bits; std::memcpy(&bits, &v, sizeof(bits)); writeU64(os, bits);
}
static uint32_t readU32(std::ifstream& is) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        int b = is.get(); if (b < 0) return 0;
        v |= static_cast<uint32_t>(b) << (i * 8);
    }
    return v;
}
static uint64_t readU64(std::ifstream& is) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        int b = is.get(); if (b < 0) return 0;
        v |= static_cast<uint64_t>(b) << (i * 8);
    }
    return v;
}
static float readF32(std::ifstream& is) {
    uint32_t bits = readU32(is); float v; std::memcpy(&v, &bits, sizeof(v)); return v;
}
static double readF64(std::ifstream& is) {
    uint64_t bits = readU64(is); double v; std::memcpy(&v, &bits, sizeof(v)); return v;
}

bool ChartParser::writeChart(const Chart& chart, const std::string& path) {
    std::ofstream os(path, std::ios::binary);
    if (!os.is_open()) return false;
    writeU32(os, kMagic);
    writeU32(os, kVersion);
    uint64_t total_size_pos = static_cast<uint64_t>(os.tellp());
    writeU64(os, 0);

    // metadata: 5 C strings
    std::vector<char> meta_buf;
    auto appendStr = [&](const std::string& s) {
        meta_buf.insert(meta_buf.end(), s.begin(), s.end());
        meta_buf.push_back('\\0');
    };
    appendStr(chart.song_id);
    appendStr(chart.difficulty);
    appendStr(chart.map_id);
    appendStr(chart.left_region);
    appendStr(chart.right_region);
    writeU32(os, static_cast<uint32_t>(meta_buf.size()));
    os.write(meta_buf.data(), meta_buf.size());

    // notes
    writeU32(os, static_cast<uint32_t>(chart.notes.size()));
    for (const auto& n : chart.notes) {
        writeU32(os, n.id);
        os.put(static_cast<uint8_t>(n.type));
        writeF64(os, n.time_sec);
        writeU64(os, n.time_ms);
        os.put(static_cast<uint8_t>(n.side));
        writeF32(os, n.position);
        writeF32(os, n.width);
        writeU32(os, n.sub_id);
        writeU64(os, n.duration_ms);
        writeU32(os, n.flags);
        writeU32(os, n.next_id);
        os.write(n.reserved, 8);
    }

    // bpm events
    writeU32(os, static_cast<uint32_t>(chart.bpm_events.size()));
    for (const auto& bpm : chart.bpm_events) {
        writeF64(os, bpm.time_sec);
        writeU64(os, bpm.time_ms);
        writeF32(os, bpm.bpm);
        writeU32(os, bpm.beat_num);
        writeU32(os, bpm.beat_den);
    }

    // scalar fields
    writeF32(os, chart.bpm);
    writeF64(os, chart.offset_sec);
    writeU64(os, static_cast<uint64_t>(chart.note_count));
    writeU64(os, chart.duration_ms);
    writeF32(os, chart.chart_constant);

    // footer magic
    writeU32(os, 0x44594E54u);

    // update total_size
    uint64_t final_pos = static_cast<uint64_t>(os.tellp());
    os.seekp(static_cast<std::streamoff>(total_size_pos));
    writeU64(os, final_pos);
    os.seekp(static_cast<std::streamoff>(final_pos));
    return os.good();
}

std::optional<Chart> ChartParser::readChart(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    if (!is.is_open()) return std::nullopt;
    if (readU32(is) != kMagic) return std::nullopt;
    uint32_t version = readU32(is);
    if (version > kVersion) return std::nullopt;
    uint64_t total_size = readU64(is);
    (void)total_size;
    Chart chart{};
    auto readCStr = [&]() -> std::string {
        std::string s; char c;
        while (is.get(c)) { if (c == '\\0') break; s.push_back(c); }
        return s;
    };
    uint32_t meta_len = readU32(is);
    if (version >= 3) {
        chart.song_id = readCStr();
        chart.difficulty = readCStr();
        chart.map_id = readCStr();
        chart.left_region = readCStr();
        chart.right_region = readCStr();
    } else {
        is.seekg(static_cast<std::streamoff>(meta_len), std::ios::cur);
    }

    uint32_t notes_count = readU32(is);
    chart.notes.reserve(notes_count);
    for (uint32_t i = 0; i < notes_count; ++i) {
        NoteData n{};
        n.id = readU32(is);
        n.type = static_cast<NoteType>(is.get());
        n.time_sec = readF64(is);
        n.time_ms = readU64(is);
        n.side = static_cast<SideType>(is.get());
        n.position = readF32(is);
        n.width = readF32(is);
        n.sub_id = readU32(is);
        n.duration_ms = readU64(is);
        n.flags = readU32(is);
        n.next_id = readU32(is);
        is.read(n.reserved, 8);
        chart.notes.push_back(n);
    }

    uint32_t bpm_count = readU32(is);
    chart.bpm_events.reserve(bpm_count);
    for (uint32_t i = 0; i < bpm_count; ++i) {
        BPMEvent bpm{};
        bpm.time_sec = readF64(is);
        bpm.time_ms = readU64(is);
        bpm.bpm = readF32(is);
        bpm.beat_num = readU32(is);
        bpm.beat_den = readU32(is);
        chart.bpm_events.push_back(bpm);
    }

    chart.bpm = readF32(is);
    chart.offset_sec = readF64(is);
    chart.note_count = static_cast<int64_t>(readU64(is));
    chart.duration_ms = readU64(is);
    chart.chart_constant = readF32(is);
    readU32(is);  // footer magic
    return chart;
}

std::string ChartParser::cachePathFrom(const std::string& original_path) {
    auto dot = original_path.rfind('.');
    if (dot == std::string::npos) return original_path + '.chart';
    return original_path.substr(0, dot) + '.chart';
}

std::optional<Chart> ChartParser::parseWithCache(const std::string& path) {
    std::string cache_path = cachePathFrom(path);
    auto cached = readChart(cache_path);
    if (cached.has_value()) return cached;
    auto chart = parse(path);
    if (!chart.has_value()) return std::nullopt;
    writeChart(*chart, cache_path);
    return chart;
}
