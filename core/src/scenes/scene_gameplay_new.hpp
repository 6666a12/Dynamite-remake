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
#include "utils/perf_monitor.h"

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
    
    // 错误状态 (§13.3)
    bool audio_error_ = false;
    
    // 性能监控 (§13.7) — 滑动窗口帧时间统计，每 120 帧输出报告
    PerfMonitor pm_;
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

    // 固定步长判定 — 将 judge tick 与显示器刷新率解耦
    static constexpr int64_t kJudgeStepMs = 2;        // 500Hz，2ms 步长
    static constexpr int64_t kMaxAccumulatorMs = 50;  // 掉帧时最多追赶 50ms（防螺旋）
    int64_t last_judge_time_ms_ = 0;                   // 上次 judge 时的音频时间
    int64_t judge_accumulator_ = 0;                    // 累积的未消耗时间 (ms)

    // ---- 辅助结构（BuildRenderFrame 分解用）----
    struct NoteProgress { float t; float dist; };
    struct LanePosition { float lane_pos; float front_width; float front_thickness; };

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

    // BuildRenderFrame 分解（§ 代码重构）
    void PopulateHUDData();
    std::unordered_map<uint32_t, bool> BuildHoldHeldMap() const;
    NoteProgress ComputeNoteProgress(float dt, float approach_time,
                                     float fall_range) const;
    float ComputeNoteAlpha(bool is_judged, float dt, int64_t audio_now_ms,
                           const NoteJudgeState* judge_state) const;
    LanePosition ComputeLanePosition(SideType side, float position) const;
    void ComputeHoldFields(NoteRenderCommand& cmd, const NoteData& note,
                           float fall_range, float approach_time) const;
    int MapJudgeState(bool is_judged, const NoteJudgeState* judge_state,
                      NoteType note_type, float dt,
                      const std::unordered_map<uint32_t, bool>& hold_held_map,
                      uint32_t note_id) const;
};
