#pragma once
#include "scene_base.h"
#include <vector>
#include <string>
#include <unordered_map>

/**
 * 选歌场景 —— 单列偏左横条卡片 + 底部难度/成绩/谱师栏
 *
 * 底部栏 (100px):
 *   < MEGA 13 >   0000523  98.52%       [ 13 ]   NOTER
 *                  BEST     ACC           MEGA
 *   点击成绩区 → Perfect/Good/Miss 弹窗
 */

// ====== 歌曲卡片 ======
struct SongCard {
    std::string id;
    std::string title;
    std::string artist;
    std::string noter;
    std::string cover_path;
    std::vector<std::pair<std::string, int>> difficulties; // {diff, level}
};

// ====== 本地最佳成绩 ======
struct SongRecord {
    int score = 0;
    int perfect = 0;
    int good = 0;
    int miss = 0;
    float accuracy = 0.0f;
};

class SceneSongSelect : public SceneBase {
public:
    void init() override;
    void enter() override;
    void update(int64_t audio_now_ms) override;
    void render(RenderBatch& batch, int64_t audio_now_ms) override;
    void handleInput(const std::vector<RawTouch>& touches, int64_t audio_now_ms) override;

private:
    enum class LoadState { IDLE, LOADING, READY, ERROR };
    LoadState load_state_ = LoadState::IDLE;

    std::vector<SongCard> songs_;
    std::unordered_map<std::string, SongRecord> records_;  // song_id → 最佳成绩

    // ====== 滚动与选中 ======
    float scroll_y_ = 0.0f;
    float target_scroll_y_ = 0.0f;
    float velocity_y_ = 0.0f;
    bool  is_dragging_ = false;
    float drag_start_y_ = 0.0f;

    int selected_index_ = 0;
    int selected_diff_index_ = 0;

    // ====== 选中动画 ======
    int   prev_selected_index_ = -1;
    float select_anim_t_ = 1.0f;

    // ====== 双击检测 ======
    int64_t last_diff_tap_ms_ = 0;
    bool    diff_tap_pending_ = false;

    // ====== 成绩弹窗 ======
    bool show_score_detail_ = false;      // 是否显示 P/G/M 弹窗

    // ====== 设置面板 ======
    bool settings_open_ = false;

    enum class SliderKind { NOTE_SPEED, OFFSET_MS };
    struct SettingSlider {
        SliderKind kind;
        const char* label;
        const char* unit;
        float min_val, max_val, step;
        float current_val;
        bool is_dragging = false;
    };
    std::vector<SettingSlider> setting_sliders_;

    enum class ToggleKind { MIRROR, BLEED, AUTO_PLAY };
    struct SettingToggle {
        ToggleKind kind;
        const char* label;
        bool value;
    };
    std::vector<SettingToggle> setting_toggles_;

    // ====== 布局常量 ======
    static constexpr float kCardX = 80.0f;
    static constexpr float kCardW = 640.0f;
    static constexpr float kCardH = 68.0f;
    static constexpr float kCardGap = 8.0f;
    static constexpr float kCardTop = 76.0f;
    static constexpr float kSelectScale = 1.08f;

    // ====== 内部方法 ======
    void initSettings();
    void syncSettingsFromConfig();
    void syncSettingsToConfig();
    float speedToFillRatio(float val) const;
    float ratioToSpeed(float ratio) const;

    void drawHeader(RenderBatch& batch);
    void drawSongCards(RenderBatch& batch);
    void drawCard(RenderBatch& batch, const SongCard& card, float base_y, bool selected, float scale);
    void drawBottomBar(RenderBatch& batch);
    void drawDifficultyBar(RenderBatch& batch, float bar_y, float bar_h);
    void drawScoreDetailPopup(RenderBatch& batch, float bar_y);
    void drawSettingsPanel(RenderBatch& batch, float bar_y);

    void clampScroll();
    int cardAtY(float py) const;

    SongRecord* currentRecord();  // 返回当前选中歌曲+难度的成绩
    void initMockRecords();       // 初始化 mock 成绩数据

    void loadSongs();
};
