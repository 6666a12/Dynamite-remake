#pragma once
#include "scene_base.h"
#include "../engine/judge_engine.h"
#include <string>

/**
 * 结算场景
 */
class SceneResult : public SceneBase {
public:
    void init() override;
    void enter() override;
    void update(int64_t audio_now_ms) override;
    void render(RenderBatch& batch, int64_t audio_now_ms) override;
    void handleInput(const std::vector<RawTouch>& touches, int64_t audio_now_ms) override;

    void setStats(const JudgeEngine::Stats& stats) { stats_ = stats; }
    void setChartInfo(const std::string& title, const std::string& difficulty, int level);

private:
    JudgeEngine::Stats stats_;
    std::string song_title_;
    std::string difficulty_;
    std::string chart_path_;     // 重试用
    std::string audio_path_;     // 重试用
    int level_ = 0;

    float anim_score_ = 0.0f;
    float anim_timer_ = 0.0f;
    bool anim_done_ = false;

    std::string getRating() const;
    void drawStatsPanel(RenderBatch& batch, int screen_w, int screen_h);
};
