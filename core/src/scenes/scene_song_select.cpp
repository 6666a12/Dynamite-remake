/**
 * 选歌场景实现 v3
 *
 * 底部栏 100px:
 *   < MEGA 13 >   0000523  98.52%       [ 13 ]   NOTER
 *                   SCORE    ACC          MEGA
 *   点击成绩区 → P/G/M 弹窗
 */

#include "scenes/scene_song_select.h"
#include "engine/render_batch.h"
#include "engine/input_manager.h"
#include "utils/logger.h"
#include "utils/config_manager.h"
#include <cmath>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "../gameplay/gameplay_ui_config.hpp"

// ============================================================
// 难度颜色
// ============================================================
static uint32_t DifficultyColor(const std::string& diff) {
    if (diff == "CASUAL") return PackColor(68,  170, 68,  255);
    if (diff == "NORMAL") return PackColor(68,  136, 255, 255);
    if (diff == "HARD")   return PackColor(255, 68,  68,  255);
    if (diff == "MEGA")   return PackColor(255, 0,   160, 255);
    if (diff == "GIGA")   return PackColor(170, 68,  255, 255);
    if (diff == "TERA")   return PackColor(255, 170, 0,   255);
    return PackColor(200, 200, 200, 255);
}

// ============================================================
// 格式化辅助
// ============================================================
static std::string fmtScore7(int score) {
    // 7 位数字，高位补 0，如 523 → "0000523"
    char buf[16];
    snprintf(buf, sizeof(buf), "%07d", score);
    return buf;
}
static std::string fmtAcc4(float acc) {
    // 4 位数字含小数点，如 98.52
    char buf[16];
    snprintf(buf, sizeof(buf), "%05.2f", acc);
    return buf;
}

// ============================================================
// 生命周期
// ============================================================

void SceneSongSelect::init() {
    initMockRecords();
    loadSongs();
    initSettings();
    syncSettingsFromConfig();
}

void SceneSongSelect::enter() {
    scroll_y_ = 0.0f;
    target_scroll_y_ = 0.0f;
    velocity_y_ = 0.0f;
    is_dragging_ = false;
    selected_index_ = 0;
    selected_diff_index_ = 0;
    prev_selected_index_ = -1;
    select_anim_t_ = 1.0f;
    diff_tap_pending_ = false;
    show_score_detail_ = false;
    syncSettingsFromConfig();
}

void SceneSongSelect::update(int64_t audio_now_ms) {
    if (!is_dragging_ && std::fabs(velocity_y_) > 2.0f) {
        target_scroll_y_ += velocity_y_;
        velocity_y_ *= 0.92f;
    } else if (!is_dragging_) {
        velocity_y_ = 0.0f;
    }
    scroll_y_ += (target_scroll_y_ - scroll_y_) * 0.25f;
    clampScroll();

    if (selected_index_ != prev_selected_index_) {
        prev_selected_index_ = selected_index_;
        select_anim_t_ = 0.0f;
        show_score_detail_ = false;  // 切歌时关闭弹窗
    }
    if (select_anim_t_ < 1.0f) {
        select_anim_t_ = std::min(1.0f, select_anim_t_ + 0.1f);
    }

    stripe_time_ms_ = audio_now_ms;
}

// ============================================================
// Mock 成绩数据
// ============================================================

void SceneSongSelect::initMockRecords() {
    // 为每首歌的每个难度生成 mock 成绩
    // key = song_id + "|" + difficulty (如 "003|MEGA")
    auto addRec = [&](const char* id, const char* diff,
                      int score, int p, int g, int m, float acc) {
        std::string key = std::string(id) + "|" + diff;
        records_[key] = {score, p, g, m, acc};
    };
    addRec("001",        "NORMAL",    523,  523,   0,   0, 100.00f);
    addRec("song_sample","HARD",   845210,  800,  34,  11,  96.80f);
    addRec("song_sample","GIGA",   112350,  450, 120,  85,  78.50f);
    addRec("003",        "MEGA",   350720,  980, 180, 110,  89.30f);
}

SongRecord* SceneSongSelect::currentRecord() {
    if (songs_.empty() || selected_index_ >= static_cast<int>(songs_.size()))
        return nullptr;
    const auto& song = songs_[selected_index_];
    if (song.difficulties.empty()) return nullptr;
    const auto& [diff_name, level] = song.difficulties[selected_diff_index_];
    (void)level;
    std::string key = song.id + "|" + diff_name;
    auto it = records_.find(key);
    return (it != records_.end()) ? &it->second : nullptr;
}

// ============================================================
// 滚动辅助
// ============================================================

void SceneSongSelect::clampScroll() {
    if (songs_.empty()) { scroll_y_ = 0.0f; target_scroll_y_ = 0.0f; return; }
    float total_h = kCardTop + songs_.size() * (kCardH + kCardGap);
    float bottom_margin = settings_open_ ? 220.0f : 140.0f;
    float max_scroll = std::max(0.0f, total_h - kDesignH + bottom_margin);
    if (target_scroll_y_ < 0.0f) target_scroll_y_ = 0.0f;
    if (target_scroll_y_ > max_scroll) target_scroll_y_ = max_scroll;
    if (scroll_y_ < 0.0f) { scroll_y_ = 0.0f; velocity_y_ = 0.0f; }
    if (scroll_y_ > max_scroll) { scroll_y_ = max_scroll; velocity_y_ = 0.0f; }
}

int SceneSongSelect::cardAtY(float py) const {
    if (songs_.empty()) return 0;
    float card_y0 = kCardTop - scroll_y_;
    int idx = static_cast<int>((py - card_y0) / (kCardH + kCardGap));
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(songs_.size())) idx = static_cast<int>(songs_.size()) - 1;
    float cy = card_y0 + idx * (kCardH + kCardGap);
    if (py < cy || py > cy + kCardH) return selected_index_;
    return idx;
}

// ============================================================
// 歌曲加载
// ============================================================

void SceneSongSelect::loadSongs() {
    load_state_ = LoadState::LOADING;
    songs_.clear();

    const char* known_dirs[] = {"001", "song_sample", "003"};
    const char* base_candidates[] = {"assets/songs/", "../assets/songs/", "../../assets/songs/"};

    std::string resolved_base;
    for (const auto* cand : base_candidates) {
        std::ifstream test(std::string(cand) + "song_sample/metadata.json");
        if (test.is_open()) { resolved_base = cand; break; }
    }
    if (resolved_base.empty()) {
        Logger::warn("SceneSongSelect: no song directories found");
        load_state_ = LoadState::ERROR;
        return;
    }

    for (const auto* dir : known_dirs) {
        std::string meta_path = resolved_base + dir + "/metadata.json";
        std::ifstream meta_file(meta_path);
        if (!meta_file.is_open()) continue;

        std::stringstream buffer;
        buffer << meta_file.rdbuf();
        std::string json_str = buffer.str();
        meta_file.close();
        if (json_str.empty()) continue;

        SongCard s;
        s.id = dir;

        auto extractStr = [&](const std::string& key) -> std::string {
            auto pos = json_str.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            pos = json_str.find("\"", pos + key.length() + 3);
            if (pos == std::string::npos) return "";
            pos++;
            auto end = json_str.find("\"", pos);
            if (end == std::string::npos) return "";
            return json_str.substr(pos, end - pos);
        };

        s.title = extractStr("title");
        s.artist = extractStr("artist");
        if (s.artist.empty()) s.artist = "Unknown";
        s.noter = extractStr("noter");
        s.cover_path = resolved_base + dir + "/cover.png";

        auto diff_pos = json_str.find("\"difficulties\"");
        if (diff_pos != std::string::npos) {
            auto obj_start = json_str.find("{", diff_pos);
            auto obj_end = json_str.find("}", obj_start);
            if (obj_start != std::string::npos && obj_end != std::string::npos) {
                std::string diff_block = json_str.substr(obj_start + 1, obj_end - obj_start - 1);
                size_t pos = 0;
                while (pos < diff_block.length()) {
                    auto key_start = diff_block.find("\"", pos);
                    if (key_start == std::string::npos) break;
                    key_start++;
                    auto key_end = diff_block.find("\"", key_start);
                    if (key_end == std::string::npos) break;
                    std::string diff_name = diff_block.substr(key_start, key_end - key_start);
                    auto lvl_pos = diff_block.find("\"level\"", key_end);
                    if (lvl_pos == std::string::npos) { pos = key_end + 1; continue; }
                    auto colon = diff_block.find(":", lvl_pos);
                    if (colon == std::string::npos) { pos = key_end + 1; continue; }
                    std::string lvl_str;
                    size_t i = colon + 1;
                    while (i < diff_block.length() && (diff_block[i] == ' ' || diff_block[i] == '\t')) i++;
                    while (i < diff_block.length() && (diff_block[i] >= '0' && diff_block[i] <= '9')) {
                        lvl_str += diff_block[i]; i++;
                    }
                    if (!lvl_str.empty())
                        s.difficulties.push_back({diff_name, std::stoi(lvl_str)});
                    pos = i;
                }
            }
        }
        if (s.difficulties.empty())
            s.difficulties.push_back({"NORMAL", 1});
        songs_.push_back(std::move(s));
    }
    load_state_ = songs_.empty() ? LoadState::ERROR : LoadState::READY;
    if (!songs_.empty()) {
        selected_index_ = 0; prev_selected_index_ = 0; select_anim_t_ = 1.0f;
    }
}

// ============================================================
// 设置数据
// ============================================================

void SceneSongSelect::initSettings() {
    setting_sliders_ = {
        { SliderKind::NOTE_SPEED, "SPEED",  "x",  0.3f, 2.5f, 0.1f, 1.0f },
        { SliderKind::OFFSET_MS,  "OFFSET", "ms", -500,  500,  5,    0    },
    };
    setting_toggles_ = {
        { ToggleKind::MIRROR,    "MIRROR",    false },
        { ToggleKind::BLEED,     "BLEED",     false },
        { ToggleKind::AUTO_PLAY, "AUTO PLAY", false },
    };
}

void SceneSongSelect::syncSettingsFromConfig() {
    auto& cfg = ConfigManager::instance();
    for (auto& s : setting_sliders_) {
        if (s.kind == SliderKind::NOTE_SPEED) s.current_val = cfg.noteSpeed();
        else s.current_val = static_cast<float>(cfg.offsetMs());
    }
    for (auto& t : setting_toggles_) {
        if (t.kind == ToggleKind::MIRROR)    t.value = cfg.mirrorMod();
        else if (t.kind == ToggleKind::BLEED) t.value = cfg.bleedMod();
        else t.value = cfg.autoPlay();
    }
}

void SceneSongSelect::syncSettingsToConfig() {
    auto& cfg = ConfigManager::instance();
    for (const auto& s : setting_sliders_) {
        if (s.kind == SliderKind::NOTE_SPEED) cfg.setNoteSpeed(s.current_val);
        else cfg.setOffsetMs(static_cast<int>(s.current_val));
    }
    for (const auto& t : setting_toggles_) {
        if (t.kind == ToggleKind::MIRROR)    cfg.setMirrorMod(t.value);
        else if (t.kind == ToggleKind::BLEED) cfg.setBleedMod(t.value);
        else cfg.setAutoPlay(t.value);
    }
    cfg.save();
}

float SceneSongSelect::speedToFillRatio(float val) const {
    if (val <= 1.0f) return (val - 0.3f) / 0.7f * 0.5f;
    return 0.5f + (val - 1.0f) / 1.5f * 0.5f;
}

float SceneSongSelect::ratioToSpeed(float ratio) const {
    ratio = std::max(0.0f, std::min(1.0f, ratio));
    float val = (ratio <= 0.5f) ? 0.3f + ratio / 0.5f * 0.7f
                                : 1.0f + (ratio - 0.5f) / 0.5f * 1.5f;
    return std::max(0.3f, std::min(2.5f, std::round(val / 0.1f) * 0.1f));
}

// ============================================================
// 渲染
// ============================================================

void SceneSongSelect::render(RenderBatch& batch, int64_t audio_now_ms) {
    (void)audio_now_ms;
    batch.submitRect(0, 0, kDesignW, kDesignH, PackColor(15, 15, 19, 255));
    drawHeader(batch);
    drawSongCards(batch);
    drawBottomBar(batch);
}

// ============================================================
// Header
// ============================================================

void SceneSongSelect::drawHeader(RenderBatch& batch) {
    const float hw = static_cast<float>(kDesignW), h = 56.0f;
    batch.submitRect(0, 0, hw, h, PackColor(15, 15, 15, 255));
    float off = -static_cast<float>(stripe_time_ms_) * 0.05f;
    batch.submitStripedRect(0, 0, hw, h, PackColor(15, 15, 15, 0),
                            PackColor(30, 30, 30, 64), -1, off);
    batch.submitText("SONG SELECT", hw * 0.5f - 120.0f, 10.0f, 1.0f,
                     PackColor(255, 255, 255, 255));
    batch.submitText("BACK", hw - 160.0f, 18.0f, 0.9f,
                     PackColor(0, 217, 255, 255));
    uint32_t sc = settings_open_ ? PackColor(59, 130, 246, 255) : PackColor(156, 163, 175, 255);
    batch.submitText("SET", hw - 80.0f, 18.0f, 0.8f, sc);
}

// ============================================================
// 歌曲卡片
// ============================================================

void SceneSongSelect::drawSongCards(RenderBatch& batch) {
    if (songs_.empty()) {
        batch.submitText("No songs found", kDesignW * 0.5f - 100.0f,
                         kDesignH * 0.5f, 1.2f, PackColor(156, 163, 175, 255));
        return;
    }
    for (size_t i = 0; i < songs_.size(); ++i) {
        float cy = kCardTop + i * (kCardH + kCardGap) - scroll_y_;
        float blim = static_cast<float>(kDesignH) - (settings_open_ ? 220.0f : 140.0f);
        if (cy + kCardH * kSelectScale < 56.0f || cy > blim) continue;

        bool sel = static_cast<int>(i) == selected_index_;
        float scale = sel ? 1.0f + select_anim_t_ * 0.08f : 1.0f;
        drawCard(batch, songs_[i], cy, sel, scale);
    }
}

void SceneSongSelect::drawCard(RenderBatch& batch, const SongCard& card,
                               float base_y, bool selected, float scale) {
    float sw = kCardW * scale, sh = kCardH * scale;
    float sx = kCardX - (sw - kCardW) * 0.5f;
    float sy = base_y - (sh - kCardH) * 0.5f;

    uint32_t bg = selected ? PackColor(32, 36, 48, 255) : PackColor(22, 22, 28, 255);
    batch.submitRoundedRect(sx, sy, sw, sh, 8.0f, bg);
    if (selected) {
        float bh = std::max(4.0f, sh - 16.0f);
        batch.submitRect(sx, sy + 8.0f, 4.0f, bh, PackColor(59, 130, 246, 255));
    }

    float tx = sx + 20.0f;
    batch.submitText(card.title, tx, sy + 14.0f, 1.0f * scale,
                     PackColor(255, 255, 255, 255));
    batch.submitText(card.artist, tx, sy + 40.0f, 0.6f * scale,
                     PackColor(156, 163, 175, 255));
}

// ============================================================
// 底部栏 (100px)
// ============================================================

void SceneSongSelect::drawBottomBar(RenderBatch& batch) {
    if (settings_open_) {
        drawSettingsPanel(batch, static_cast<float>(kDesignH) - 160.0f);
    } else {
        const float bar_h = 100.0f;
        float bar_y = static_cast<float>(kDesignH) - bar_h;
        drawDifficultyBar(batch, bar_y, bar_h);
        if (show_score_detail_) {
            drawScoreDetailPopup(batch, bar_y);
        }
    }
}

void SceneSongSelect::drawDifficultyBar(RenderBatch& batch, float bar_y, float bar_h) {
    // 背景 + 斜纹
    batch.submitRect(0, bar_y, static_cast<float>(kDesignW), bar_h,
                     PackColor(18, 18, 22, 255));
    batch.submitRect(0, bar_y, static_cast<float>(kDesignW), 2.0f,
                     PackColor(40, 40, 40, 255));
    float off = static_cast<float>(stripe_time_ms_) * 0.05f;
    batch.submitStripedRect(0, bar_y, static_cast<float>(kDesignW), bar_h,
                            PackColor(18, 18, 22, 0),
                            PackColor(36, 36, 42, 64), 1, off);

    if (songs_.empty() || selected_index_ >= static_cast<int>(songs_.size())) return;
    const auto& song = songs_[selected_index_];

    // ====== 左侧: 难度选择器 (名+等级在一起) + 谱师 ======
    float left_x = 50.0f;

    if (!song.difficulties.empty()) {
        if (song.difficulties.size() > 1)
            batch.submitText("<", left_x, bar_y + 20.0f, 1.1f,
                             PackColor(156, 163, 175, 255));

        const auto& [diff_name, level] = song.difficulties[selected_diff_index_];
        uint32_t dc = DifficultyColor(diff_name);

        // 难度名标签
        float tag_x = left_x + 35.0f;
        batch.submitRoundedRect(tag_x, bar_y + 16.0f, 80.0f, 26.0f, 13.0f, dc);
        batch.submitText(diff_name.substr(0, 4), tag_x + 8.0f, bar_y + 21.0f, 0.55f,
                         PackColor(255, 255, 255, 255));

        // 等级数字 — 紧挨着难度名, 大字
        float lv_x = tag_x + 90.0f;
        batch.submitText(std::to_string(level), lv_x, bar_y + 12.0f, 1.4f,
                         PackColor(255, 255, 255, 255), true);

        // 谱师 — 等级右侧
        std::string nt = song.noter.empty() ? "" : "NOTER: " + song.noter;
        if (!nt.empty()) {
            batch.submitText(nt, lv_x + 36.0f, bar_y + 40.0f, 0.5f,
                             PackColor(130, 138, 150, 255));
        }

        if (song.difficulties.size() > 1)
            batch.submitText(">", lv_x + 200.0f, bar_y + 20.0f, 1.1f,
                             PackColor(156, 163, 175, 255));
    }

    // ====== 右下角: 最佳成绩 ======
    SongRecord* rec = currentRecord();
    float right_x = static_cast<float>(kDesignW) - 60.0f;  // 右边界

    if (rec && rec->score > 0) {
        // SCORE — 7 位数字, 偏灰白, 右对齐
        std::string sc = fmtScore7(rec->score);
        float sc_w = 7.0f * 10.0f;  // 7 个等宽数字的近似宽度
        batch.submitText(sc, right_x - sc_w, bar_y + 14.0f, 0.9f,
                         PackColor(190, 198, 210, 255), true);

        // ACC — 4 位+%, 更小更灰
        std::string ac = fmtAcc4(rec->accuracy) + "%";
        float ac_w = 6.0f * 8.0f;
        batch.submitText(ac, right_x - ac_w, bar_y + 56.0f, 0.55f,
                         PackColor(150, 158, 170, 255), true);

        // 标签
        batch.submitText("BEST", right_x - sc_w, bar_y + 36.0f, 0.45f,
                         PackColor(107, 114, 128, 255));

        // 可点击区域高亮
        float click_x = right_x - 180.0f;
        if (show_score_detail_) {
            batch.submitRect(click_x, bar_y + 8.0f, 180.0f, bar_h - 16.0f,
                             PackColor(45, 50, 60, 120));
        }
    } else {
        batch.submitText("NO RECORD", right_x - 90.0f, bar_y + 30.0f, 0.65f,
                         PackColor(107, 114, 128, 255));
    }
}

// ============================================================
// 成绩详情弹窗 (P/G/M)
// ============================================================

void SceneSongSelect::drawScoreDetailPopup(RenderBatch& batch, float bar_y) {
    SongRecord* rec = currentRecord();
    if (!rec) return;

    // 弹窗位置: 成绩区正上方 (右下角)
    float popup_w = 200.0f, popup_h = 100.0f;
    float right_x = static_cast<float>(kDesignW) - 60.0f;
    float popup_x = right_x - popup_w;
    float popup_y = bar_y - popup_h - 8.0f;

    // 背景
    batch.submitRoundedRect(popup_x, popup_y, popup_w, popup_h, 10.0f,
                            PackColor(28, 28, 36, 245));
    batch.submitRect(popup_x, popup_y, popup_w, 2.0f,
                     PackColor(59, 130, 246, 180));

    // P/G/M 三行
    auto drawRow = [&](const char* label, int val, float y, uint32_t clr) {
        batch.submitText(label, popup_x + 16.0f, y, 0.55f, PackColor(156, 163, 175, 255));
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", val);
        batch.submitText(buf, popup_x + popup_w - 60.0f, y, 0.6f, clr, true);
    };

    float ry = popup_y + 16.0f;
    drawRow("PERFECT", rec->perfect, ry,  PackColor(255, 215, 0, 255));     // 金色
    drawRow("GOOD",    rec->good,    ry + 28.0f, PackColor(100, 200, 255, 255)); // 浅蓝
    drawRow("MISS",    rec->miss,    ry + 56.0f, PackColor(255, 100, 100, 255)); // 红色
}

// ============================================================
// 设置面板
// ============================================================

void SceneSongSelect::drawSettingsPanel(RenderBatch& batch, float bar_y) {
    const float bar_h = 160.0f;
    batch.submitRect(0, bar_y, static_cast<float>(kDesignW), bar_h,
                     PackColor(20, 20, 20, 255));
    batch.submitRect(0, bar_y, static_cast<float>(kDesignW), 2.0f,
                     PackColor(59, 130, 246, 180));
    float off = static_cast<float>(stripe_time_ms_) * 0.05f;
    batch.submitStripedRect(0, bar_y, static_cast<float>(kDesignW), bar_h,
                            PackColor(20, 20, 20, 0),
                            PackColor(40, 40, 40, 64), 1, off);

    const float tw = 290.0f, th = 8.0f, tr = 12.0f;
    const float sry = bar_y + 18.0f, sty = sry + 28.0f;
    for (size_t i = 0; i < setting_sliders_.size(); ++i) {
        const auto& s = setting_sliders_[i];
        float bx = (i == 0) ? 40.0f : 570.0f;
        batch.submitText(s.label, bx, sry + 4.0f, 0.65f, PackColor(156, 163, 175, 255));
        float tx = bx + 90.0f;
        batch.submitRoundedRect(tx, sty - th * 0.5f, tw, th, th * 0.5f, PackColor(55, 55, 65, 255));
        float r = (s.kind == SliderKind::NOTE_SPEED) ? speedToFillRatio(s.current_val)
                  : (s.current_val - s.min_val) / (s.max_val - s.min_val);
        float fw = tw * r;
        if (fw > 2.0f)
            batch.submitRoundedRect(tx, sty - th * 0.5f, fw, th, th * 0.5f, PackColor(59, 130, 246, 220));
        float thx = tx + fw;
        batch.submitRoundedRect(thx - tr, sty - tr, tr * 2, tr * 2, tr,
                                s.is_dragging ? PackColor(255, 255, 255, 255)
                                              : PackColor(255, 255, 255, 200));
        std::ostringstream vs;
        if (s.kind == SliderKind::OFFSET_MS) vs << std::fixed << std::setprecision(0) << s.current_val << s.unit;
        else vs << std::fixed << std::setprecision(1) << s.current_val << s.unit;
        batch.submitText(vs.str(), tx + tw + 14.0f, sry + 4.0f, 0.7f,
                         PackColor(255, 255, 255, 255), true);
    }

    const float tgy = bar_y + 76.0f;
    for (size_t i = 0; i < setting_toggles_.size(); ++i) {
        const auto& tg = setting_toggles_[i];
        float tx = 40.0f + i * 124.0f;
        batch.submitText(tg.label, tx, tgy + 4.0f, 0.55f, PackColor(156, 163, 175, 255));
        float by = tgy + 18.0f;
        if (tg.value) {
            batch.submitRoundedRect(tx, by, 100.0f, 32.0f, 16.0f, PackColor(59, 130, 246, 255));
            batch.submitText("ON", tx + 32.0f, by + 6.0f, 0.6f, PackColor(255, 255, 255, 255));
        } else {
            batch.submitRoundedRect(tx, by, 100.0f, 32.0f, 16.0f, PackColor(55, 55, 65, 255));
            batch.submitText("OFF", tx + 26.0f, by + 6.0f, 0.6f, PackColor(156, 163, 175, 255));
        }
    }
}

// ============================================================
// 输入处理
// ============================================================

void SceneSongSelect::handleInput(const std::vector<RawTouch>& touches,
                                  int64_t audio_now_ms) {
    static std::unordered_map<int64_t, float> t_start_y;
    static std::unordered_map<int64_t, bool>  t_is_drag;

    const float settings_bar_y = static_cast<float>(kDesignH) - 160.0f;
    const float diff_bar_y = static_cast<float>(kDesignH) - 100.0f;

    for (const auto& t : touches) {
        float px = t.x * kDesignW;
        float py = t.y * kDesignH;

        if (t.is_new && t.is_down) {
            // ---- Header ----
            if (HitTest(px, py, static_cast<float>(kDesignW) - 180.0f, 0.0f,
                        80.0f, kHeaderH)) {
                transition_request_.type = Transition::PUSH;
                transition_request_.target_scene_id = static_cast<int>(SceneID::MAIN_MENU);
                return;
            }
            if (HitTest(px, py, static_cast<float>(kDesignW) - 80.0f, 0.0f,
                        80.0f, kHeaderH)) {
                settings_open_ = !settings_open_;
                if (!settings_open_) syncSettingsToConfig();
                return;
            }

            // ---- 成绩弹窗: 点击弹窗外关闭 ----
            if (show_score_detail_ && py < diff_bar_y - 108.0f) {
                show_score_detail_ = false;
            }

            // ---- 设置面板 ----
            if (settings_open_ && py >= settings_bar_y) {
                float ly = py - settings_bar_y;
                if (ly >= 18.0f && ly <= 66.0f) {
                    for (size_t i = 0; i < setting_sliders_.size(); ++i) {
                        auto& s = setting_sliders_[i];
                        float bx = (i == 0) ? 40.0f : 570.0f;
                        float tx = bx + 90.0f;
                        if (HitTest(px, py, tx, settings_bar_y + 18.0f,
                                    290.0f, 48.0f)) {
                            s.is_dragging = true;
                            if (s.kind == SliderKind::NOTE_SPEED)
                                s.current_val = ratioToSpeed((px - tx) / 290.0f);
                            else {
                                float rr = std::max(0.0f, std::min(1.0f, (px - tx) / 290.0f));
                                s.current_val = std::max(s.min_val, std::min(s.max_val,
                                    std::round((s.min_val + rr * (s.max_val - s.min_val)) / s.step) * s.step));
                            }
                            return;
                        }
                    }
                }
                if (ly >= 76.0f && ly <= 124.0f) {
                    for (size_t i = 0; i < setting_toggles_.size(); ++i) {
                        float ttx = 40.0f + i * 124.0f;
                        if (HitTest(px, py, ttx, settings_bar_y + 94.0f,
                                    100.0f, 32.0f)) {
                            setting_toggles_[i].value = !setting_toggles_[i].value;
                            syncSettingsToConfig();
                            return;
                        }
                    }
                }
                return;
            }

            // ---- 底部栏 ----
            if (!settings_open_ && py >= diff_bar_y) {
                // 左箭头（HitTest 膨胀到 ≥44dp）
                if (HitTest(px, py, 40.0f, diff_bar_y, 40.0f, 44.0f)
                    && selected_index_ < static_cast<int>(songs_.size())
                    && songs_[selected_index_].difficulties.size() > 1) {
                    selected_diff_index_--;
                    if (selected_diff_index_ < 0)
                        selected_diff_index_ = static_cast<int>(songs_[selected_index_].difficulties.size()) - 1;
                    return;
                }
                // 右箭头
                if (HitTest(px, py, 225.0f, diff_bar_y, 40.0f, 44.0f)
                    && selected_index_ < static_cast<int>(songs_.size())
                    && songs_[selected_index_].difficulties.size() > 1) {
                    selected_diff_index_++;
                    if (selected_diff_index_ >= static_cast<int>(songs_[selected_index_].difficulties.size()))
                        selected_diff_index_ = 0;
                    return;
                }

                // 双击难度标签 → 进游玩
                if (HitTest(px, py, 80.0f, diff_bar_y, 150.0f, 44.0f)) {
                    if (diff_tap_pending_ && (audio_now_ms - last_diff_tap_ms_ < 200)) {
                        diff_tap_pending_ = false;
                        transition_request_.type = Transition::PUSH;
                        transition_request_.target_scene_id = static_cast<int>(SceneID::GAMEPLAY);
                        // 填入谱面/音频路径
                        if (!songs_.empty() && selected_index_ < static_cast<int>(songs_.size())) {
                            const auto& s = songs_[selected_index_];
                            std::string diff_lower = s.difficulties[selected_diff_index_].first;
                            std::transform(diff_lower.begin(), diff_lower.end(), diff_lower.begin(), ::tolower);
                            transition_request_.payload.chart_path =
                                "assets/songs/" + s.id + "/chart_" + diff_lower + ".xml";
                            transition_request_.payload.audio_path =
                                "assets/songs/" + s.id + "/bgm.mp3";
                        }
                        return;
                    }
                    diff_tap_pending_ = true;
                    last_diff_tap_ms_ = audio_now_ms;
                    return;
                }

                // 点击成绩区 (右下角) → 切换弹窗
                if (HitTest(px, py, static_cast<float>(kDesignW) - 240.0f,
                            diff_bar_y, 180.0f, 44.0f)) {
                    show_score_detail_ = !show_score_detail_;
                    return;
                }
                return;
            }

            // ---- 卡片区 ----
            t_start_y[t.finger_id] = t.y;
            t_is_drag[t.finger_id] = false;
            is_dragging_ = true;
            drag_start_y_ = t.y;
            velocity_y_ = 0.0f;

        } else if (t.is_down) {
            if (settings_open_) {
                for (auto& s : setting_sliders_) {
                    if (s.is_dragging) {
                        float bx = (&s == &setting_sliders_[0]) ? 40.0f : 570.0f;
                        float tx = bx + 90.0f;
                        if (s.kind == SliderKind::NOTE_SPEED)
                            s.current_val = ratioToSpeed((px - tx) / 290.0f);
                        else {
                            float rr = std::max(0.0f, std::min(1.0f, (px - tx) / 290.0f));
                            s.current_val = std::max(s.min_val, std::min(s.max_val,
                                std::round((s.min_val + rr * (s.max_val - s.min_val)) / s.step) * s.step));
                        }
                        break;
                    }
                }
                continue;
            }

            float dy = (t.y - t_start_y[t.finger_id]) * kDesignH;
            if (std::fabs(dy) > 12.0f) {
                t_is_drag[t.finger_id] = true;
                target_scroll_y_ -= dy;
                t_start_y[t.finger_id] = t.y;
            }

        } else if (!t.is_down) {
            for (auto& s : setting_sliders_)
                if (s.is_dragging) { s.is_dragging = false; syncSettingsToConfig(); }

            if (is_dragging_) {
                velocity_y_ = -(t.y - drag_start_y_) * kDesignH * 3.0f;
                is_dragging_ = false;
            }

            auto it = t_is_drag.find(t.finger_id);
            if (it != t_is_drag.end() && !it->second) {
                if (py > 56.0f && py < diff_bar_y && px >= kCardX - 30.0f && px <= kCardX + kCardW + 30.0f) {
                    int idx = cardAtY(py);
                    if (idx == selected_index_) {
                        transition_request_.type = Transition::PUSH;
                        transition_request_.target_scene_id = static_cast<int>(SceneID::GAMEPLAY);
                        // 填入谱面/音频路径
                        if (selected_index_ < static_cast<int>(songs_.size())) {
                            const auto& s = songs_[selected_index_];
                            std::string diff_lower = s.difficulties[selected_diff_index_].first;
                            std::transform(diff_lower.begin(), diff_lower.end(), diff_lower.begin(), ::tolower);
                            transition_request_.payload.chart_path =
                                "assets/songs/" + s.id + "/chart_" + diff_lower + ".xml";
                            transition_request_.payload.audio_path =
                                "assets/songs/" + s.id + "/bgm.mp3";
                        }
                    } else {
                        selected_index_ = idx;
                        selected_diff_index_ = 0;
                    }
                }
            }
            t_start_y.erase(t.finger_id);
            t_is_drag.erase(t.finger_id);
        }
    }
    clampScroll();
}
