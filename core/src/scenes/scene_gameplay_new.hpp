#pragma once
#include "scene_base.h"
#include "../engine/chart_parser.h"
#include "../engine/judge_engine.h"
#include "../engine/audio_engine.h"
#include "../utils/texture.h"

#include "gameplay/gameplay_ui_config.hpp"
#include "gameplay/gameplay_render_command.hpp"
#include "gameplay/lane_coordinate.hpp"
#include "gameplay/i_renderer.hpp"
#include "gameplay/judge_line_renderer.hpp"
#include "gameplay/tap_note_renderer.hpp"
#include "gameplay/hold_note_renderer.hpp"
#include "gameplay/hit_effect_renderer.hpp"
#include "gameplay/hud_renderer.hpp"
#include "gameplay/footer_renderer.hpp"
#include "gameplay/background_renderer.hpp"

#include <memory>
#include <deque>

/**
 * 游玩场景 —— 基于 v1.1 渲染规范的完整重构
 *
 * 架构分层：
 *   顶层：SceneGameplay（业务逻辑 + 数据组装）
 *   中层：LaneCoordinateTransformer（坐标转换）
 *   底层：各 Renderer（绘图）+ IRenderer（抽象接口）
 *   数据层：GameplayRenderFrame（每帧注入渲染包）
 *
 * 材质支持：每个 Renderer 优先使用材质纹理，无纹理时回退到纯色图形
 */
class SceneGameplay : public SceneBase {
public:
    void init() override;
    void enter() override;
    void exit() override;
    void update(int64_t audio_now_ms) override;
    void render(RenderBatch& batch, int64_t audio_now_ms) override;
    void handleInput(const std::vector<RawTouch>& touches, int64_t audio_now_ms) override;

    void loadChart(const std::string& path, const std::string& song_path);

    int64_t currentTimeMs() const override {
        return audio_ ? static_cast<int64_t>(audio_->clock().nowMs()) : SceneBase::currentTimeMs();
    }
    void onPause() override { if (audio_) audio_->pause(); }
    void onResume() override { if (audio_) audio_->play(); }

private:
    // ---- 核心子系统 ----
    std::unique_ptr<AudioEngine> audio_;
    std::unique_ptr<JudgeEngine> judge_;
    std::unique_ptr<Chart> chart_;

    bool is_playing_ = false;
    float note_speed_ = 1.3f;

    // ---- 视口变换（等比例缩放 + 居中）----
    float screen_w_ = 0.0f;  // 物理屏幕宽
    float screen_h_ = 0.0f;  // 物理屏幕高
    float viewport_scale_ = 1.0f;
    float viewport_off_x_ = 0.0f;
    float viewport_off_y_ = 0.0f;

    // ---- 渲染子系统（新架构 v1.1）----
    std::shared_ptr<IRenderer> renderer_;
    std::unique_ptr<BackgroundRenderer> bg_renderer_;
    std::unique_ptr<JudgeLineRenderer> judge_line_renderer_;
    std::unique_ptr<TapNoteRenderer> tap_renderer_;
    std::unique_ptr<HoldNoteRenderer> hold_renderer_;
    std::unique_ptr<HitEffectRenderer> effect_renderer_;
    std::unique_ptr<HudRenderer> hud_renderer_;
    std::unique_ptr<FooterRenderer> footer_renderer_;

    LaneCoordinateTransformer transformer_;

    // ---- 渲染数据（每帧构造）----
    GameplayRenderFrame frame_;
    std::vector<NoteRenderCommand> note_cmds_;
    std::vector<HitEffectCommand> effect_cmds_;

    // ---- 资源 ----
    std::unique_ptr<Texture> cover_tex_;
    std::string current_song_title_;
    std::string current_difficulty_;
    uint32_t current_diff_color_ = GameplayUI::CLR_DIFF_NORMAL;

    // ---- Note 判定状态追踪 ----
    struct NoteJudgeState {
        uint32_t note_id;
        JudgeType result;
        int64_t judged_time_ms;
        bool expired = false;
    };
    std::unordered_map<uint32_t, NoteJudgeState> note_judge_states_;

    // ---- 打击特效（旧系统兼容）----
    struct HitEffect {
        float x, y, width, height;
        uint32_t color;
        float lifetime, max_lifetime;
        NoteType note_type;
    };
    std::deque<HitEffect> hit_effects_;

        // 上一帧音频时间戳（用于计算两帧间真实 dt）
    int64_t last_frame_time_ms_ = 0;

    // ---- 方法 ----
    void BuildRenderFrame(int64_t audio_now_ms);
    void SyncNoteJudgments(const std::vector<JudgeResult>& results);
    void UpdateTransformer();
    float CalcApproachTimeMs() const;
    float CalcApproachTimeMsForBpm(float bpm) const;
    float CalcFallRange() const;

    // 打击特效（兼容旧系统）
    void SpawnHitEffect(SideType side, JudgeType type, NoteType note_type,
                        int screen_w, int screen_h);
    void UpdateHitEffects(float dt);
};
