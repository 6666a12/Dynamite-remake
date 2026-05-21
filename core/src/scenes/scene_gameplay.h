#pragma once
#include "scene_base.h"
#include "../engine/audio_engine.h"
#include "../engine/judge_engine.h"
#include "../engine/chart_parser.h"
#include "../utils/texture.h"
#include <memory>
#include <deque>

/**
 * 游玩场景 —— 三侧下落式核心
 * 
 * 时间基准：AudioClock::nowMs()
 */
class SceneGameplay : public SceneBase {
public:
    void init() override;
    void enter() override;
    void exit() override;
    void update(int64_t audio_now_ms) override;
    void render(RenderBatch& batch, int64_t audio_now_ms) override;
    void handleInput(const std::vector<RawTouch>& touches, int64_t audio_now_ms) override;

    // 加载谱面并开始游戏
    void loadChart(const std::string& path, const std::string& song_path);

    // 暴露音频时钟引用，供主循环获取时间基准
    const AudioClock& audioClock() const { return audio_->clock(); }

    // 暂停/恢复音频（用于应用进入后台/前台）
    void pauseAudio() { if (audio_) audio_->pause(); }
    void resumeAudio() { if (audio_) audio_->play(); }

private:
    std::unique_ptr<AudioEngine> audio_;
    std::unique_ptr<JudgeEngine> judge_;
    std::unique_ptr<Chart> chart_;

    bool is_playing_ = false;
    int64_t start_time_ms_ = 0;
    float note_speed_ = 1.3f; // 落速

    // 歌曲封面背景纹理
    std::unique_ptr<Texture> cover_tex_;

    // HUD 状态
    int last_perfect_ = 0, last_good_ = 0, last_miss_ = 0;
    int last_combo_ = 0;
    float score_display_ = 0.0f;

    // 打击特效：在判定线位置显示对应 Light 特效
    struct HitEffect {
        float x, y;
        float width, height;
        uint32_t color;
        float lifetime;      // 剩余生命周期（秒）
        float max_lifetime;
        NoteType note_type;  // 用于选择特效纹理
    };
    std::deque<HitEffect> hit_effects_;
    void spawnHitEffect(SideType side, JudgeType type, NoteType note_type,
                        int screen_w, int screen_h);

    // ---- Note 判定状态追踪 ----
    struct NoteJudgeState {
        uint32_t note_id;
        JudgeType result;
        int64_t judged_time_ms;
        bool expired = false;
    };
    std::vector<NoteJudgeState> note_judge_states_;

    void drawBackground(RenderBatch& batch, int screen_w, int screen_h);
    void drawTracks(RenderBatch& batch, int screen_w, int screen_h);
    void drawNotes(RenderBatch& batch, int screen_w, int screen_h, int64_t audio_now_ms);
    void drawHUD(RenderBatch& batch, int screen_w, int screen_h, int64_t audio_now_ms);
    void drawJudgeEffects(RenderBatch& batch);
    void drawHitEffects(RenderBatch& batch, int screen_w, int screen_h,
                        float dt, int64_t audio_now_ms);
};
