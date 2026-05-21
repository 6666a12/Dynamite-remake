#pragma once
#include "scene_base.h"
#include <vector>
#include <string>

/**
 * 选歌场景 —— 横向封面卡片网格
 */
struct SongCard {
    std::string id;
    std::string title;
    std::string artist;
    std::string cover_path;
    std::vector<std::pair<std::string, int>> difficulties; // {diff, level}
};

class SceneSongSelect : public SceneBase {
public:
    void init() override;
    void enter() override;
    void update(int64_t audio_now_ms) override;
    void render(RenderBatch& batch, int64_t audio_now_ms) override;
    void handleInput(const std::vector<RawTouch>& touches, int64_t audio_now_ms) override;

private:
    std::vector<SongCard> songs_;
    float scroll_y_ = 0.0f;
    float target_scroll_y_ = 0.0f;

    void loadSongs();
    void drawHeader(RenderBatch& batch);
    void drawSongGrid(RenderBatch& batch, int screen_w, int screen_h);
    void drawBottomBar(RenderBatch& batch, int screen_w, int screen_h);
};
