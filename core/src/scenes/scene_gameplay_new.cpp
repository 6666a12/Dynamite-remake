/**
 * 游玩场景 —— 基于 v1.1 渲染规范的重构实现
 *
 * 架构：
 *   1. SceneGameplay 负责业务逻辑（谱面加载、判定、音频）
 *   2. 每帧调用 BuildRenderFrame() 将 NoteData + JudgeResult 转换为
 *      NoteRenderCommand / HitEffectCommand（渲染指令）
 *   3. 各 Renderer 接收渲染指令 + LaneCoordinateTransformer 进行绘制
 *
 * 材质支持：
 *   每个 Renderer 优先查询 IRenderer 提供的纹理引用，
 *   有纹理时用 DrawTexture()，无纹理时回退到 DrawRoundedRect()/DrawRect()
 */

#include "scenes/scene_gameplay_new.hpp"
#include "engine/render_batch.h"
#include "engine/input_manager.h"
#include "utils/logger.h"
#include "utils/config_manager.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include "../gameplay/gameplay_ui_config.hpp"



// 跨场景共享结算数据（与旧版兼容）
// (全局变量已移除，改用 TransitionRequest.payload)

// 本帧触摸缓存
static std::vector<RawTouch> s_current_touches;

// ============================================================
// 生命周期
// ============================================================

void SceneGameplay::init() {
    // 音频
    audio_ = std::make_unique<AudioEngine>();
    if (!audio_->init(44100, 2, 128)) {
        Logger::error("AudioEngine 初始化失败");
    }

    // 判定
    judge_ = std::make_unique<JudgeEngine>();

    // 渲染器不会在 init 中持有 batch，因为 render() 时才传入
    // 子渲染器在 render() 时延迟初始化
}

void SceneGameplay::enter() {
    is_playing_ = true;
    last_frame_time_ms_ = 0;  // 重置帧计时，避免首次 update 使用旧值
    last_judge_time_ms_ = 0;  // 重置固定步长计时器
    judge_accumulator_ = 0;
    note_judge_states_.clear();
    hit_effects_.clear();
    frame_ = GameplayRenderFrame{};

    if (audio_) {
        audio_->play();
    } else {
        audio_error_ = true;
        Logger::warn("SceneGameplay: audio unavailable, running silent");
    }
}

void SceneGameplay::exit() {
    is_playing_ = false;
    if (audio_) audio_->stop();
}

void SceneGameplay::loadChart(const std::string& path, const std::string& song_path) {
    auto chart_opt = ChartParser::parseWithCache(path);
    if (chart_opt) {
        chart_ = std::make_unique<Chart>(*chart_opt);
        judge_->loadChart(chart_->notes);
        transition_request_.payload.song_title = chart_->song_id;
        transition_request_.payload.difficulty = chart_->difficulty;
        transition_request_.payload.chart_path = path;
        transition_request_.payload.audio_path = song_path;
        current_song_title_ = chart_->song_id;
        current_difficulty_ = chart_->difficulty;

        // 难度颜色
        if (current_difficulty_ == "CASUAL") current_diff_color_ = PackColor(68, 170, 68, 255);
        else if (current_difficulty_ == "NORMAL") current_diff_color_ = GameplayUI::CLR_DIFF_NORMAL;
        else if (current_difficulty_ == "HARD")   current_diff_color_ = GameplayUI::CLR_DIFF_HARD;
        else if (current_difficulty_ == "MEGA")   current_diff_color_ = PackColor(255, 0, 160, 255);
        else if (current_difficulty_ == "GIGA")   current_diff_color_ = PackColor(170, 68, 255, 255);
        else if (current_difficulty_ == "TERA")   current_diff_color_ = PackColor(255, 170, 0, 255);
        else current_diff_color_ = GameplayUI::CLR_DIFF_NORMAL;
    } else {
        Logger::error("谱面加载失败: {}", path);
    }

    if (audio_) {
        if (!audio_->loadSong(song_path)) {
            Logger::error("音频加载失败: {}", song_path);
        }
    }

    // 加载封面
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        std::string dir = path.substr(0, last_slash + 1);
        cover_tex_ = std::make_unique<Texture>(dir + "cover.png");
        if (!cover_tex_->valid()) {
            Logger::warn("封面加载失败: {}", dir + "cover.png");
            cover_tex_.reset();
        }
    }
}

// ============================================================
// 更新
// ============================================================

void SceneGameplay::update(int64_t audio_now_ms) {
    if (!is_playing_ || !judge_) return;
    pm_.beginFrame();

    // ---- 固定步长判定循环（与显示器刷新率解耦） ----
    // 积累自上次 update 以来的音频时间，以 2ms 步长推进 judge
    if (last_judge_time_ms_ == 0) {
        last_judge_time_ms_ = audio_now_ms;   // 首帧：避免从 0 累积
    }
    int64_t elapsed = audio_now_ms - last_judge_time_ms_;
    if (elapsed < 0) elapsed = 0;             // 防御：时钟不应倒退
    judge_accumulator_ += elapsed;
    last_judge_time_ms_ = audio_now_ms;

    // 上限防螺旋：掉帧严重时最多追赶 50ms，丢弃多余时间
    if (judge_accumulator_ > kMaxAccumulatorMs) {
        judge_accumulator_ = kMaxAccumulatorMs;
    }

    while (judge_accumulator_ >= kJudgeStepMs) {
        // 计算本次子步的音频时间点（从累加器尾部往前推）
        int64_t judge_time = audio_now_ms - judge_accumulator_ + kJudgeStepMs;
        judge_->update(judge_time, s_current_touches);
        judge_accumulator_ -= kJudgeStepMs;
    }

    // ---- 同步 Note 判定状态 ----
    SyncNoteJudgments(judge_->frameResults());

    // ---- 生成打击特效 ----
    for (const auto& res : judge_->frameResults()) {
        SpawnHitEffect(res.side, res.type,
                       static_cast<NoteType>(res.type), kDesignW, kDesignH);
    }

    // ---- 使用两帧音频时间戳差值作为真实 dt，驱动特效生命周期 ----
    if (last_frame_time_ms_ > 0) {
        float real_dt = static_cast<float>(audio_now_ms - last_frame_time_ms_) / 1000.0f;
        // 夹紧上限防止跳帧导致特效瞬间消失
        real_dt = std::min(real_dt, 0.05f);
        UpdateHitEffects(real_dt);
    }
    last_frame_time_ms_ = audio_now_ms;

    // ---- 清理过期状态 ----
    for (auto it = note_judge_states_.begin(); it != note_judge_states_.end(); ) {
        if (it->second.expired) {
            it = note_judge_states_.erase(it);
        } else {
            ++it;
        }
    }

    // ---- 谱面结束检测 ----
    auto stats = judge_->currentStats();
    // 检测谱面结束：优先使用音频播放结束信号，回退到谱面时长
    bool chart_ended = false;
    if (audio_ && audio_->hasReachedEnd()) {
        chart_ended = true;
    } else if (chart_ && audio_now_ms > static_cast<int64_t>(chart_->duration_ms) + 2000) {
        chart_ended = true;
    }
    
    if (chart_ended) {
        auto& p = transition_request_.payload;
        p.perfect = stats.perfect;
        p.good = stats.good;
        p.miss = stats.miss;
        p.max_combo = stats.max_combo;
        p.score = stats.score;
        p.accuracy = stats.accuracy;
        p.is_full_combo = stats.is_full_combo;
        p.is_all_perfect = stats.is_all_perfect;
#if defined(__ANDROID__)
        transition_request_.type = Transition::POP;
#else
        transition_request_.type = Transition::REPLACE;
        transition_request_.target_scene_id = static_cast<int>(SceneID::RESULT);
#endif
    }
}

void SceneGameplay::SyncNoteJudgments(const std::vector<JudgeResult>& results) {
    for (const auto& res : results) {
        if (!res.is_hold_tail) {
            auto it = note_judge_states_.find(res.note_id);
            if (it == note_judge_states_.end() || it->second.expired) {
                note_judge_states_[res.note_id] = {res.note_id, res.type, res.judged_at_ms, false};
            }
        }
    }
}

// ============================================================
// 渲染
// ============================================================

void SceneGameplay::render(RenderBatch& batch, int64_t audio_now_ms) {
    // ---- 延迟初始化渲染子系统 ----
    if (!renderer_) {
        renderer_ = std::make_shared<RenderBatchRenderer>(batch);
        bg_renderer_ = std::make_unique<BackgroundRenderer>(renderer_);
        judge_line_renderer_ = std::make_unique<JudgeLineRenderer>(renderer_);
        tap_renderer_ = std::make_unique<TapNoteRenderer>(renderer_);
        hold_renderer_ = std::make_unique<HoldNoteRenderer>(renderer_);
        effect_renderer_ = std::make_unique<HitEffectRenderer>(renderer_);
        hud_renderer_ = std::make_unique<HudRenderer>(renderer_);
        footer_renderer_ = std::make_unique<FooterRenderer>(renderer_);
    }

    // ---- 计算视口变换（等比例 16:9 缩放 + 居中）----
    // 从 ConfigManager 读取已知的物理屏幕参数（首次启动时检测，固定不变）
    {
        const auto& cfg = ConfigManager::instance();
        screen_w_ = static_cast<float>(cfg.screenWidth());
        screen_h_ = static_cast<float>(cfg.screenHeight());
        const float design_w = static_cast<float>(kDesignW);
        const float design_h = static_cast<float>(kDesignH);
        const float scale_x = screen_w_ / design_w;
        const float scale_y = screen_h_ / design_h;
        viewport_scale_ = std::min(scale_x, scale_y);
        viewport_off_x_ = (screen_w_ - design_w * viewport_scale_) * 0.5f;
        viewport_off_y_ = (screen_h_ - design_h * viewport_scale_) * 0.5f;
        renderer_->SetViewportTransform(viewport_scale_, viewport_off_x_, viewport_off_y_);
    }

    // ---- 更新变换器（屏幕尺寸可能变化）----
    UpdateTransformer();

    // ---- 构建本帧渲染数据 ----
    BuildRenderFrame(audio_now_ms);

    // ---- 按 v1.1 规范的渲染顺序 ----
    // 1. 背景
    bg_renderer_->Render(cover_tex_.get(), frame_.cover_darken,
                         kDesignW, kDesignH);

    // 2. 轨道遮罩（三条判定线背景区域）
        // Dynamite 实际布局：
    //   DOWN 区域: x=[108,1812], y=[0,945]（底判线以上全部区域）
    //   LEFT 区域: x=[0,108], y=[0,945]（左判线左侧区域）
    //   RIGHT 区域: x=[1812,1920], y=[0,945]（右判线右侧区域）
    {
        constexpr float kLeftJudgeX   = static_cast<float>(kDesignW) * 0.05625f;
        constexpr float kRightJudgeX  = static_cast<float>(kDesignW) * 0.94375f;
        constexpr float kBottomJudgeY = static_cast<float>(kDesignH) * 0.875f;
        const uint32_t track_bg = PackColor(35, 35, 35, 200);
        const uint32_t border = PackColor(255, 215, 0, 120);

        // DOWN 区域 (中央大区域)
        renderer_->DrawRect(kLeftJudgeX, 0.0f, kRightJudgeX - kLeftJudgeX, kBottomJudgeY, track_bg);
        renderer_->DrawRect(kLeftJudgeX, 0.0f, 2.0f, kBottomJudgeY, border);
        renderer_->DrawRect(kRightJudgeX - 2.0f, 0.0f, 2.0f, kBottomJudgeY, border);
        // LEFT 区域 (左判线左侧)
        renderer_->DrawRect(0.0f, 0.0f, kLeftJudgeX, kBottomJudgeY, track_bg);
        renderer_->DrawRect(kLeftJudgeX, 0.0f, 2.0f, kBottomJudgeY, border);
        // RIGHT 区域 (右判线右侧)
        renderer_->DrawRect(kRightJudgeX, 0.0f, static_cast<float>(kDesignW) - kRightJudgeX, kBottomJudgeY, track_bg);
        renderer_->DrawRect(kRightJudgeX - 2.0f, 0.0f, 2.0f, kBottomJudgeY, border);
    }

    // 3. Note 层（按 distance 从大到小排序，远的先画）
    for (const auto& note_cmd : note_cmds_) {
        if (note_cmd.note_type == NoteType::TAP || note_cmd.note_type == NoteType::MULTI || note_cmd.note_type == NoteType::SLIDE) {
            tap_renderer_->Render(note_cmd, transformer_);
        } else if (note_cmd.note_type == NoteType::HOLD_HEAD || note_cmd.note_type == NoteType::HOLD_BODY || note_cmd.note_type == NoteType::HOLD_TAIL) {
            hold_renderer_->Render(note_cmd, transformer_);
        }
    }

    // 4. 判定线
    judge_line_renderer_->Render(kDesignW, kDesignH,
                                 transformer_.bottom_judge_y,
                                 transformer_.left_judge_x,
                                 transformer_.right_judge_x);

    // 5. 判定特效
    for (const auto& eff_cmd : effect_cmds_) {
        effect_renderer_->Render(eff_cmd, transformer_);
    }

        // 6. HUD + Footer
    hud_renderer_->Render(frame_, kDesignW);
    footer_renderer_->Render(frame_.song_title, frame_.difficulty_label,
                             frame_.difficulty_color, kDesignW, kDesignH);

    pm_.endFrame();
    if (pm_.shouldReport()) {
        char buf[80];
        snprintf(buf, sizeof(buf), "FPS perf: avg=%.1fms max=%.1fms | frame #%d",
                 pm_.avgMs(), pm_.maxMs(), pm_.frameCount());
        Logger::info("{}", std::string(buf));
    }
}

// ============================================================
// 构建渲染帧（核心：将 NoteData + 判定状态 → NoteRenderCommand）
// ============================================================

// ---- 子步骤 1: HUD 数据 ----
void SceneGameplay::PopulateHUDData() {
    auto stats = judge_->currentStats();
    frame_.perfect_count = stats.perfect;
    frame_.good_count = stats.good;
    frame_.miss_count = stats.miss;
    frame_.combo = stats.combo;
    frame_.max_combo = stats.max_combo;
    frame_.accuracy = static_cast<float>(stats.accuracy);
    frame_.score = stats.score;
    frame_.song_title = current_song_title_;
    frame_.difficulty_label = current_difficulty_;
    frame_.difficulty_color = current_diff_color_;
    frame_.cover_darken = cover_tex_ ? 0.5f : 0.0f;
}

// ---- 子步骤 2: Hold 按住状态查表 ----
std::unordered_map<uint32_t, bool> SceneGameplay::BuildHoldHeldMap() const {
    std::unordered_map<uint32_t, bool> m;
    if (judge_) {
        const auto& hold_states = judge_->currentHoldStates();
        m.reserve(hold_states.size());
        for (const auto& hs : hold_states) {
            m[hs.note_id] = hs.is_held;
        }
    }
    return m;
}

// ---- 子步骤 3: 下落进度 (t) 和像素距离 (dist) ----
SceneGameplay::NoteProgress SceneGameplay::ComputeNoteProgress(
    float dt, float approach_time, float fall_range) const
{
    static constexpr float kPostJudgeSlowdownMs = 800.0f;
    static constexpr float kExtraDistRatio = 0.30f;

    float t;
    if (dt > 0.0f) {
        t = 1.0f - (dt / approach_time);
        t = std::max(0.0f, t);
    } else {
        float elapsed_after = -dt;
        float slowdown_ratio = std::min(1.0f, elapsed_after / kPostJudgeSlowdownMs);
        float eased = slowdown_ratio * (2.0f - slowdown_ratio);
        t = 1.0f + eased * kExtraDistRatio;
    }
    return {t, t * fall_range};
}

// ---- 子步骤 4: Alpha 透明度衰减 ----
float SceneGameplay::ComputeNoteAlpha(
    bool is_judged, float dt, int64_t audio_now_ms,
    const NoteJudgeState* judge_state) const
{
    static constexpr float kPostJudgeSlowdownMs = 800.0f;

    if (is_judged && judge_state) {
        float elapsed = static_cast<float>(audio_now_ms - judge_state->judged_time_ms);
        if (elapsed >= kPostJudgeSlowdownMs) return -1.0f;  // 已过期
        float fade_t = elapsed / kPostJudgeSlowdownMs;
        return 1.0f - fade_t * fade_t;
    }
    if (!is_judged && dt < 0.0f) {
        float elapsed_after = -dt;
        float fade_t = std::min(1.0f, elapsed_after / kPostJudgeSlowdownMs);
        float alpha = 1.0f - fade_t * 0.6f;
        return std::max(0.4f, alpha);
    }
    return 1.0f;
}

// ---- 子步骤 5: 轨道坐标 (lane_pos + front width/thickness) ----
SceneGameplay::LanePosition SceneGameplay::ComputeLanePosition(
    SideType side, float position) const
{
    static constexpr float kTrackWidth   = static_cast<float>(kDesignW) * 0.8875f;
    static constexpr float kTrackHeight  = static_cast<float>(kDesignH) * 0.875f;

    LanePosition lp{};
    if (side == SideType::DOWN) {
        lp.lane_pos = (position - 0.5f) * kTrackWidth * 0.8f;
        lp.front_width = GameplayUI::NOTE_WIDTH_BASE;
        lp.front_thickness = GameplayUI::NOTE_THICKNESS_BASE;
    } else {
        lp.lane_pos = (position - 0.5f) * kTrackHeight * 0.6f;
        lp.front_width = GameplayUI::NOTE_WIDTH_BASE * GameplayUI::SIDE_WIDTH_SCALE;
        lp.front_thickness = GameplayUI::NOTE_THICKNESS_BASE;
    }
    return lp;
}

// ---- 子步骤 6: 长条像素长度 ----
void SceneGameplay::ComputeHoldFields(
    NoteRenderCommand& cmd, const NoteData& note,
    float fall_range, float approach_time) const
{
    if (note.type != NoteType::HOLD_HEAD && note.type != NoteType::HOLD_TAIL) return;
    if (note.duration_ms > 0) {
        float dur_s = static_cast<float>(note.duration_ms) / 1000.0f;
        cmd.hold_length_front = dur_s * note_speed_ * fall_range / (approach_time / 1000.0f);
        cmd.hold_length_front = std::max(20.0f, cmd.hold_length_front);
    }
    cmd.hold_progress = 0.0f;
}

// ---- 子步骤 7: 判定状态 → 渲染状态码 ----
int SceneGameplay::MapJudgeState(
    bool is_judged, const NoteJudgeState* judge_state,
    NoteType note_type, float dt,
    const std::unordered_map<uint32_t, bool>& hold_held_map,
    uint32_t note_id) const
{
    auto is_hold = [](NoteType t) {
        return t == NoteType::HOLD_HEAD || t == NoteType::HOLD_TAIL;
    };

    if (is_judged && judge_state) {
        if (judge_state->result == JudgeType::MISS) return 4;

        bool held = false;
        auto it = hold_held_map.find(note_id);
        if (it != hold_held_map.end()) held = it->second;

        if (is_hold(note_type) && held) return 5;  // Holding

        return (judge_state->result == JudgeType::GOOD) ? 3 : 2;
    }

    // 未判定
    return (dt <= 0.0f) ? 1 : 0;
}

// ---- 主函数（重组后 ~50 行）----
void SceneGameplay::BuildRenderFrame(int64_t audio_now_ms) {
    note_cmds_.clear();
    effect_cmds_.clear();
    frame_.notes.clear();
    frame_.effects.clear();

    if (!chart_) return;

    // 1. HUD
    PopulateHUDData();

    // 2. 预计算
    const float approach_time = CalcApproachTimeMsForBpm(chart_->bpm);
    const float fall_range = CalcFallRange();
    note_cmds_.reserve(chart_->notes.size());
    auto hold_held_map = BuildHoldHeldMap();

    // 3. NoteData → NoteRenderCommand
    for (const auto& note : chart_->notes) {
        if (note.type == NoteType::HOLD_BODY) continue;

        float dt = static_cast<float>(note.time_ms - audio_now_ms);
        if (dt > approach_time) continue;

        auto judge_it = note_judge_states_.find(note.id);
        bool is_judged = (judge_it != note_judge_states_.end() && !judge_it->second.expired);
        const NoteJudgeState* js = is_judged ? &judge_it->second : nullptr;

        // Alpha（返回 -1 表示过期）
        float alpha = ComputeNoteAlpha(is_judged, dt, audio_now_ms, js);
        if (alpha < 0.0f) {
            const_cast<NoteJudgeState*>(js)->expired = true;
            continue;
        }

        auto [t, dist] = ComputeNoteProgress(dt, approach_time, fall_range);

        NoteRenderCommand cmd{};
        cmd.id = static_cast<int32_t>(note.id);
        cmd.note_type = note.type;
        cmd.lane = static_cast<LaneType>(static_cast<uint8_t>(note.side));
        cmd.alpha = alpha;
        cmd.scale = 1.0f;

        auto lp = ComputeLanePosition(note.side, note.position);
        cmd.lane_pos = lp.lane_pos;
        cmd.distance = dist;
        cmd.front_width = lp.front_width;
        cmd.front_thickness = lp.front_thickness;

        ComputeHoldFields(cmd, note, fall_range, approach_time);
        cmd.judge_state = MapJudgeState(is_judged, js, note.type, dt,
                                        hold_held_map, note.id);

        note_cmds_.push_back(cmd);
    }

    // 4. 排序（远的先画）
    std::stable_sort(note_cmds_.begin(), note_cmds_.end(),
        [](const NoteRenderCommand& a, const NoteRenderCommand& b) {
            return a.distance > b.distance;
        });
    frame_.notes = note_cmds_;

    // 5. 特效
    for (const auto& eff : hit_effects_) {
        HitEffectCommand ec{};
        ec.lifetime = eff.lifetime;
        ec.max_lifetime = eff.max_lifetime;
        ec.judgment_type = ((eff.color & 0xFF) >= 200) ? 2 : 0;
        effect_cmds_.push_back(ec);
    }
    frame_.effects = effect_cmds_;
}

// ============================================================
// 变换器更新
// ============================================================

void SceneGameplay::UpdateTransformer() {
    // Dynamite 真实布局 (1920x1080 设计分辨率):
    //   LEFT 判线: x = 108 (距左 5.625%)
    //   RIGHT 判线: x = 1812 (距右 5.625%)
    //   BOTTOM 判线: y = 945 (距底 12.5%)
    //   左/右判线从底判线延伸到屏幕顶部
    constexpr float kLeftJudgeX   = static_cast<float>(kDesignW) * 0.05625f;  // 108
    constexpr float kRightJudgeX  = static_cast<float>(kDesignW) * 0.94375f; // 1812
    constexpr float kBottomJudgeY = static_cast<float>(kDesignH) * 0.875f;   // 945

    transformer_.bottom_judge_y = kBottomJudgeY;
    transformer_.left_judge_x = kLeftJudgeX;
    transformer_.right_judge_x = kRightJudgeX;
    transformer_.lane_center_y = kBottomJudgeY * 0.5f;  // 侧轨道沿判线的中心 Y
    transformer_.bottom_center_x = static_cast<float>(kDesignW) * 0.5f;
}

float SceneGameplay::CalcApproachTimeMs() const {
    return 1500.0f / note_speed_;
}

float SceneGameplay::CalcApproachTimeMsForBpm(float bpm) const {
    (void)bpm;
    return CalcApproachTimeMs();
}

float SceneGameplay::CalcFallRange() const {
    // 下落范围 = 从底判线到屏幕顶部的距离（DOWN 向上飞范围）
    // 同时也是 LEFT/RIGHT 从判线到屏幕边缘的水平距离
    return static_cast<float>(kDesignH) * 0.875f;  // 945px
}

// ============================================================
// 打击特效（兼容旧系统）
// ============================================================

void SceneGameplay::SpawnHitEffect(SideType side, JudgeType type,
                                    NoteType note_type,
                                    int screen_w, int screen_h) {
    constexpr float kBottomJudgeY = 945.0f;
    constexpr float kLeftJudgeX   = 108.0f;
    constexpr float kRightJudgeX  = 1812.0f;
    constexpr float kLaneCenterY  = 945.0f * 0.5f;
    constexpr float kEffectWidth  = 60.0f;

    HitEffect eff{};
    eff.width = kEffectWidth;
    eff.height = 8.0f;
    eff.max_lifetime = 0.12f;
    eff.lifetime = eff.max_lifetime;
    eff.note_type = note_type;

    switch (type) {
        case JudgeType::PERFECT: eff.color = PackColor(255, 255, 255, 255); break;
        case JudgeType::GOOD:    eff.color = PackColor(255, 220, 100, 255); break;
        case JudgeType::MISS:    eff.color = PackColor(255, 50, 50, 255); break;
    }

    switch (side) {
        case SideType::LEFT:
            eff.x = kLeftJudgeX - eff.width * 0.5f;
            eff.y = kLaneCenterY - eff.height * 0.5f;
            break;
        case SideType::DOWN:
            eff.x = static_cast<float>(screen_w) * 0.5f - eff.width * 0.5f;
            eff.y = kBottomJudgeY - eff.height * 0.5f;
            break;
        case SideType::RIGHT:
            eff.x = kRightJudgeX - eff.width * 0.5f;
            eff.y = kLaneCenterY - eff.height * 0.5f;
            break;
    }

    hit_effects_.push_back(eff);
}

void SceneGameplay::UpdateHitEffects(float dt) {
    for (auto it = hit_effects_.begin(); it != hit_effects_.end();) {
        it->lifetime -= dt;
        if (it->lifetime <= 0.0f) {
            it = hit_effects_.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================
// 输入处理
// ============================================================

void SceneGameplay::handleInput(const std::vector<RawTouch>& touches,
                                 int64_t audio_now_ms) {
    (void)audio_now_ms;
    
    // 将物理触摸坐标映射回 16:9 逻辑坐标
    // 等比例缩放 + 居中的逆变换：
    //   逻辑_x = (物理_x * screen_w - offset_x) / (design_w * scale)
    //   如果逻辑坐标不在 [0, 1] 范围内，说明触摸在黑边区域，忽略。
    const float design_w = static_cast<float>(kDesignW);
    const float design_h = static_cast<float>(kDesignH);
    
    s_current_touches.clear();
    s_current_touches.reserve(touches.size());

    for (const auto& t : touches) {
        RawTouch mapped = t;
        
        // 逆映射：物理归一化坐标 → 16:9 逻辑归一化坐标
        // 物理坐标 (tx, ty) 对应物理像素 (tx * screen_w, ty * screen_h)
        // 在 viewport 区域内的 16:9 逻辑坐标：
        //   logic_x = (物理_x)  // 因为物理 x 是 0~1，直接对应 16:9 区域的 x 比例
        // 更精确：
        float phys_px_x = t.x * screen_w_;
        float phys_px_y = t.y * screen_h_;
        
        // 减去 viewport 偏移，转换到 viewport 坐标系
        float vp_x = (phys_px_x - viewport_off_x_) / viewport_scale_;
        float vp_y = (phys_px_y - viewport_off_y_) / viewport_scale_;
        
        // 归一化到 [0, 1]
        mapped.x = vp_x / design_w;
        mapped.y = vp_y / design_h;
        
        // 黑边区域忽略
        if (mapped.x < 0.0f || mapped.x > 1.0f ||
            mapped.y < 0.0f || mapped.y > 1.0f) {
            continue;
        }
        
        s_current_touches.push_back(mapped);
    }

    for (const auto& t : s_current_touches) {
        if (t.is_new && t.is_down) {
            if (t.x < 0.1f && t.y < 0.1f) {
                transition_request_.type = Transition::POP;
                return;
            }
        }
    }
}
