/**
 * @file chart_parser.cpp
 * @brief 谱面解析器实现 —— 原生支持 DynaMaker XML/ZIP/JSON 格式 + BPM 变速
 */

#include "engine/chart_parser.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <regex>

// ============================================================
// ZIP 文件格式（EOCD 中央目录读取，仅支持 store 无压缩）
// ============================================================
static constexpr uint32_t kZipLocalFileHeaderSig = 0x04034b50;
static constexpr uint32_t kZipCentralDirSig      = 0x02014b50;
static constexpr uint32_t kZipEOCDSig            = 0x06054b50;

static inline uint16_t readU16LE(const uint8_t* p) { return p[0] | (p[1] << 8); }
static inline uint32_t readU32LE(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

bool ChartParser::parseZipFile(const std::string& zip_path,
                                std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::ifstream is(zip_path, std::ios::binary);
    if (!is.is_open()) return false;

    is.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(is.tellg());
    is.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(size);
    is.read(reinterpret_cast<char*>(buf.data()), size);
    is.close();

    // 1. 从文件末尾搜索 EOCD 记录（comment 最大 65535 字节）
    size_t eocd_off = 0;
    size_t search_start = (size >= 22) ? size - 22 : 0;
    for (size_t i = search_start; ; --i) {
        if (readU32LE(&buf[i]) == kZipEOCDSig) {
            eocd_off = i;
            break;
        }
        if (i == 0) break;
    }
    if (readU32LE(&buf[eocd_off]) != kZipEOCDSig) return false;

    uint16_t disk_num      = readU16LE(&buf[eocd_off + 4]);
    uint16_t cd_start_disk = readU16LE(&buf[eocd_off + 6]);
    uint16_t disk_entries  = readU16LE(&buf[eocd_off + 8]);
    uint16_t total_entries = readU16LE(&buf[eocd_off + 10]);
    uint32_t cd_size       = readU32LE(&buf[eocd_off + 12]);
    uint32_t cd_off        = readU32LE(&buf[eocd_off + 16]);

    if (disk_num != 0 || cd_start_disk != 0 || disk_entries != total_entries) return false;
    if (cd_off + cd_size > size) return false;

    // 2. 遍历中央目录条目
    size_t pos = cd_off;
    for (uint16_t i = 0; i < total_entries; ++i) {
        if (pos + 46 > size) return false;
        if (readU32LE(&buf[pos]) != kZipCentralDirSig) return false;

        uint16_t compression   = readU16LE(&buf[pos + 10]);
        uint32_t comp_size     = readU32LE(&buf[pos + 20]);
        uint16_t name_len      = readU16LE(&buf[pos + 28]);
        uint16_t extra_len     = readU16LE(&buf[pos + 30]);
        uint16_t comment_len   = readU16LE(&buf[pos + 32]);
        uint32_t local_off     = readU32LE(&buf[pos + 42]);

        if (pos + 46 + name_len + extra_len + comment_len > size) return false;

        std::string filename(reinterpret_cast<const char*>(&buf[pos + 46]), name_len);
        if (filename.empty() || filename.back() == '/') {
            pos += 46 + name_len + extra_len + comment_len;
            continue;
        }

        if (compression != 0) {
            // 仅支持无压缩存储（DynaMaker ZIP 均为 STORE）
            return false;
        }

        // 从 local file header 定位文件数据
        size_t data_off = local_off + 30
                        + readU16LE(&buf[local_off + 26])   // name_len
                        + readU16LE(&buf[local_off + 28]);  // extra_len
        if (data_off + comp_size > size) return false;

        std::vector<uint8_t> file_data(buf.begin() + data_off,
                                        buf.begin() + data_off + comp_size);
        files.emplace_back(std::move(filename), std::move(file_data));

        pos += 46 + name_len + extra_len + comment_len;
    }
    return !files.empty();
}

// ============================================================
// beatToSec —— 简单线性转换（无变速时使用）
// ============================================================

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

    // ---- 基础元数据 ----
    if (std::regex_search(xml_str, m, std::regex(R"(<m_barPerMin>([^<]+)</m_barPerMin>)")))
        chart.bpm = std::stof(m[1].str()) * 4.0f;  // barPerMin → 实际 BPM（×4，4/4拍）
    if (std::regex_search(xml_str, m, std::regex(R"(<m_timeOffset>([^<]+)</m_timeOffset>)")))
        chart.offset_sec = std::stod(m[1].str());
    if (std::regex_search(xml_str, m, std::regex(R"(<m_leftRegion>([^<]+)</m_leftRegion>)")))
        chart.left_region = m[1].str();
    if (std::regex_search(xml_str, m, std::regex(R"(<m_rightRegion>([^<]+)</m_rightRegion>)")))
        chart.right_region = m[1].str();
    if (std::regex_search(xml_str, m, std::regex(R"(<m_mapID>([^<]+)</m_mapID>)")))
        chart.map_id = m[1].str();

    // 从 map_id 提取 song_id 和难度（格式: _map_<歌名>_<难度代号>）
    // 例如 _map_И00._G → song_id="И00.", difficulty=GIGA
    if (!chart.map_id.empty() && chart.map_id.find("_map_") == 0) {
        std::string inner = chart.map_id.substr(5);  // 去掉 "_map_"
        auto last_underscore = inner.rfind('_');
        if (last_underscore != std::string::npos && last_underscore + 1 == inner.size() - 1) {
            chart.song_id = inner.substr(0, last_underscore);
            char dc = inner.back();
            if (dc == 'C') chart.difficulty = "CASUAL";
            else if (dc == 'N') chart.difficulty = "NORMAL";
            else if (dc == 'H') chart.difficulty = "HARD";
            else if (dc == 'M') chart.difficulty = "MEGA";
            else if (dc == 'G') chart.difficulty = "GIGA";
            else if (dc == 'T') chart.difficulty = "TERA";
            else { chart.song_id = chart.map_id; chart.difficulty = "UNKNOWN"; }
        } else {
            chart.song_id = inner;
            chart.difficulty = "UNKNOWN";
        }
    }

    // ---- 解析 BPM 变速事件（在 notes 之前）----
    using BpmChangePoint = std::pair<double, float>;
    std::vector<BpmChangePoint> bpm_changes;

    {
        std::regex bpm_sec_re(R"(<m_bpmchange>([\s\S]*?)</m_bpmchange>)",
                              std::regex::icase);
        std::smatch sm_bpm;
        if (std::regex_search(xml_str, sm_bpm, bpm_sec_re)) {
            std::string bpm_content = sm_bpm[1].str();
            std::regex cbp_re(R"(<CBpmchange>([\s\S]*?)</CBpmchange>)",
                             std::regex::icase);
            auto it  = std::sregex_iterator(bpm_content.begin(), bpm_content.end(), cbp_re);
            auto end = std::sregex_iterator();
            for (; it != end; ++it) {
                std::string c = (*it)[1].str();
                double beat_pos = 0.0;
                float  bpm_val  = chart.bpm;
                std::smatch sm;
                if (std::regex_search(c, sm, std::regex(R"(<m_time>([^<]+)</m_time>)")))
                    beat_pos = std::stod(sm[1].str());
                if (std::regex_search(c, sm, std::regex(R"(<m_value>([^<]+)</m_value>)")))
                    bpm_val = std::stof(sm[1].str()) * 4.0f;  // barPerMin → BPM
                bpm_changes.emplace_back(beat_pos, bpm_val);
            }
        }
    }
    std::stable_sort(bpm_changes.begin(), bpm_changes.end());

    // ---- 可变 BPM 的 beat → 秒转换 lambda ----
    auto beatToSecVar = [&](double beat_time) -> double {
        if (chart.bpm <= 0.0f) return 0.0;
        if (bpm_changes.empty())
            return beat_time * (60.0 / static_cast<double>(chart.bpm)) - chart.offset_sec;

        double cur_beat = 0.0;
        double cur_sec  = 0.0;
        float  cur_bpm  = chart.bpm;

        for (const auto& [chg_beat, chg_bpm] : bpm_changes) {
            if (chg_beat > beat_time) break;
            if (chg_beat < cur_beat) continue;

            double seg_beats = chg_beat - cur_beat;
            cur_sec += seg_beats * (60.0 / static_cast<double>(cur_bpm));
            cur_bpm  = chg_bpm;
            cur_beat = chg_beat;
        }
        double seg_beats = beat_time - cur_beat;
        cur_sec += seg_beats * (60.0 / static_cast<double>(cur_bpm));
        return cur_sec - chart.offset_sec;
    };

    // ---- 填充 chart.bpm_events ----
    chart.bpm_events.clear();
    chart.bpm_events.reserve(bpm_changes.size());
    for (const auto& [bp, bv] : bpm_changes) {
        double t_sec = beatToSecVar(bp);
        BPMEvent evt{};
        evt.time_sec = t_sec;
        evt.time_ms  = static_cast<uint64_t>(t_sec * 1000.0);
        evt.bpm      = bv;
        evt.beat_num = 4;
        evt.beat_den = 4;
        chart.bpm_events.push_back(evt);
    }

    // ---- 解析 notes（使用 beatToSecVar）----
    auto parseNoteSection = [&](const std::string& sec, SideType side) {
        std::regex sec_re("<" + sec + R"([\s\S]*?</)" + sec + ">)",
                         std::regex::icase);
        std::smatch sm_sec;
        if (!std::regex_search(xml_str, sm_sec, sec_re)) return;
        std::string sec_content = sm_sec[0].str();

        std::regex note_re(R"(<CMapNoteAsset>([\s\S]*?)</CMapNoteAsset>)",
                          std::regex::icase);
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
                note.time_sec = beatToSecVar(bt);
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

    // ---- HOLD duration 计算 ----
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

    // ---- chart_constant（XML 中无此字段，通过难度估算）----
    if (chart.difficulty == "CASUAL") chart.chart_constant = 1.0f;
    else if (chart.difficulty == "NORMAL") chart.chart_constant = 3.0f;
    else if (chart.difficulty == "HARD")   chart.chart_constant = 6.0f;
    else if (chart.difficulty == "MEGA")   chart.chart_constant = 10.0f;
    else if (chart.difficulty == "GIGA")   chart.chart_constant = 14.0f;
    else if (chart.difficulty == "TERA")   chart.chart_constant = 17.0f;
    else chart.chart_constant = 0.0f;

    chart.note_count = (int64_t)chart.notes.size();
    chart.duration_ms = 0;
    for (auto& n : chart.notes) {
        uint64_t end = n.time_ms + n.duration_ms;
        if (end > chart.duration_ms) chart.duration_ms = end;
    }

    // ---- 校验边界合法性 ----
    if (!validateBounds(chart)) return std::nullopt;

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
// ZIP 包解析
// ============================================================

/// 从 info.json 内容解析出通用元数据；被 parsePackageInfo / parsePackage 共用
static std::optional<DynaMakerPackage> parseInfoJsonStr(const std::string& json_str) {
    try {
        auto j = nlohmann::json::parse(json_str);
        DynaMakerPackage pkg{};
        if (j.contains("musicName"))     pkg.music_name     = j["musicName"].get<std::string>();
        if (j.contains("musicComposer"))  pkg.music_composer  = j["musicComposer"].get<std::string>();
        if (j.contains("noterName"))      pkg.noter_name      = j["noterName"].get<std::string>();
        if (j.contains("musicFile"))      pkg.music_file      = j["musicFile"].get<std::string>();
        if (j.contains("cover"))          pkg.cover_file      = j["cover"].get<std::string>();
        if (j.contains("previewFile"))    pkg.preview_file    = j["previewFile"].get<std::string>();
        if (j.contains("difficulties") && j["difficulties"].is_object()) {
            for (auto& [diff, level] : j["difficulties"].items()) {
                pkg.difficulties[diff] = diff + ".xml";
            }
        }
        if (!pkg.music_name.empty()) return pkg;
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

/// 在 ZIP 文件列表中查找 info.json
static std::string findInfoJson(
    std::vector<std::pair<std::string, std::vector<uint8_t>>>& files)
{
    for (auto& [name, data] : files) {
        if (name == "info.json" || name.find("info.json") != std::string::npos) {
            return {data.begin(), data.end()};
        }
    }
    return {};
}

/// 在 ZIP 文件列表中查找指定难度的 XML
static std::string findXmlByName(
    std::vector<std::pair<std::string, std::vector<uint8_t>>>& files,
    const std::string& diff)
{
    std::string target = diff + ".xml";
    for (auto& [name, data] : files) {
        std::string upper_name = name;
        std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper);
        std::string upper_target = target;
        std::transform(upper_target.begin(), upper_target.end(), upper_target.begin(), ::toupper);
        if (upper_name == upper_target || name.find(target) != std::string::npos) {
            return {data.begin(), data.end()};
        }
    }
    return {};
}

std::optional<DynaMakerPackage> ChartParser::parsePackageInfo(const std::string& zip_path) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    if (!parseZipFile(zip_path, files)) return std::nullopt;

    std::string json_str = findInfoJson(files);
    if (json_str.empty()) return std::nullopt;

    return parseInfoJsonStr(json_str);
}

std::optional<std::vector<Chart>> ChartParser::parsePackage(const std::string& zip_path) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    if (!parseZipFile(zip_path, files)) return std::nullopt;

    // 解析 info.json（复用 parseInfoJsonStr）
    std::string json_str = findInfoJson(files);
    auto pkg_opt = parseInfoJsonStr(json_str);
    if (!pkg_opt) return std::nullopt;

    const auto& pkg = *pkg_opt;
    std::string cache_base = cachePathFrom(zip_path);

    // 对每个难度，优先读 .chart 缓存，否则解析 XML 并写入缓存
    std::vector<Chart> charts;
    charts.reserve(pkg.difficulties.size());

    for (const auto& [diff, xml_filename] : pkg.difficulties) {
        // 尝试读取 per-difficulty 缓存
        std::string diff_cache = cachePathFrom(zip_path, diff);
        auto cached = readChart(diff_cache);
        if (cached) {
            charts.push_back(*cached);
            continue;
        }

        // 从 ZIP 中提取 XML 并解析
        std::string xml_str = findXmlByName(files, diff);
        if (xml_str.empty()) continue;

        auto chart_opt = parseXmlString(xml_str);
        if (!chart_opt) continue;

        // 用包级元数据填充（优先于 XML 中的 map_id）
        chart_opt->song_id = pkg.music_name;
        if (!pkg.noter_name.empty()) {
            // noter_name 暂时通过 difficulty 附带（后续可扩展 Chart 结构）
        }

        writeChart(*chart_opt, diff_cache);
        charts.push_back(*chart_opt);
    }

    if (charts.empty()) return std::nullopt;
    return charts;
}

// ============================================================
// JSON 解析
// ============================================================

std::optional<Chart> ChartParser::parseDynaMakerJson(const std::string& json_str) {
    try {
        auto j = nlohmann::json::parse(json_str);
        Chart chart{};
        chart.bpm = 240.0f;  // 默认 BPM=240（JSON 无 barPerMin，按常规节奏设定）
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
        meta_buf.push_back('\0');
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
        while (is.get(c)) { if (c == '\0') break; s.push_back(c); }
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

std::string ChartParser::cachePathFrom(const std::string& original_path,
                                        const std::string& difficulty) {
    auto dot = original_path.rfind('.');
    std::string base = (dot == std::string::npos)
        ? original_path : original_path.substr(0, dot);
    if (!difficulty.empty()) {
        return base + "_" + difficulty + ".chart";
    }
    return base + ".chart";
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
