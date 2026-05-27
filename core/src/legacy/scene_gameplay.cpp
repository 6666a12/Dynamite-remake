/**
 * 游玩场景实现 —— 三侧下落式音游核心
 * 
 * 布局（基于 1080x1920 设计分辨率）：
 * - 左侧轨道：屏幕左 25%，Note 从上向下落到判定线
 * - 下方轨道：屏幕底侧横条，Note 从下向上飞到判定线
 * - 右侧轨道：屏幕右 25%，Note 从上向下落到判定线
 * 
 * 渲染层次（由底到顶）：
 * 1. 背景（歌曲封面暗化 50%）
 * 2. 三侧轨道底 + 判定线
 * 3. Note 精灵
 * 4. HUD（统计、分数、Combo）
 * 5. 底部信息条
 */

#include "scenes/scene_gameplay.h"
#include "engine/render_batch.h"
#include "engine/input_manager.h"
#include "engine/judge_engine.h"
#include "engine/chart_parser.h"
#include "utils/logger.h"
#include <cmath>

static constexpr int kDesignW = 1920;
static constexpr int kDesignH = 1080;

static inline uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r)
         | (static_cast<uint32_t>(g) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

// ============================================================
// 跨场景共享：游玩结束后的结算数据
// ============================================================

JudgeEngine::Stats s_pending_stats;
std::string s_pending_song_title;
std::string s_pending_difficulty;

// 本帧触摸缓存（头文件无此成员，用文件级静态变量过渡）
static std::vector<RawTouch> s_current_touches;

// ============================================================
// 生命周期
// ============================================================

void SceneGameplay::init() {
    audio_ = std::make_unique<AudioEngine>();
    if (!audio_->init(44100, 2, 128)) {
        Logger::error("AudioEngine 初始化失败");
    }
    judge_ = std::make_unique<JudgeEngine>();
}

void SceneGameplay::enter() {
    is_playing_ = true;
    last_perfect_ = last_good_ = last_miss_ = last_combo_ = 0;
    score_display_ = 0.0f;
    if (audio_) {
        audio_->play();
    }
}

void SceneGameplay::exit() {
    is_playing_ = false;
    if (audio_) {
        audio_->stop();
    }
}

void SceneGameplay::loadChart(const std::string& path, const std::string& song_path) {
    auto chart_opt = ChartParser::parseWithCache(path);
    if (chart_opt) {
        chart_ = std::make_unique<Chart>(*chart_opt);
        judge_->loadChart(chart_->notes);
        s_pending_song_title = chart_->song_id;
        s_pending_difficulty = chart_->difficulty;
    } else {
        Logger::error("谱面加载失败：{}", path);
    }

    if (audio_) {
        if (!audio_->loadSong(song_path)) {
            Logger::error("音频加载失败：{}", song_path);
        } else {
            Logger::info("音频加载成功：{}", song_path);
        }
    }

    // 加载歌曲封面：从谱面目录推断 cover.png 路径
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        std::string dir = path.substr(0, last_slash + 1);
        cover_tex_ = std::make_unique<Texture>(dir + "cover.png");
        if (!cover_tex_->valid()) {
            Logger::warn("封面加载失败：{}", dir + "cover.png");
            cover_tex_.reset();
        }
    }
}

// ============================================================
// 更新
// ============================================================

void SceneGameplay::update(int64_t audio_now_ms) {
    if (!is_playing_ || !judge_) {
        return;
    }

    // 将触摸传递给判定引擎（音频时间驱动）
    judge_->update(audio_now_ms, s_current_touches);

    // 同步 HUD 显示值
    auto stats = judge_->currentStats();
    last_perfect_ = stats.perfect;
    last_good_    = stats.good;
    last_miss_    = stats.miss;
    last_combo_   = stats.combo;
    score_display_ = static_cast<float>(stats.score);

    // 生成打击特效
    for (const auto& res : judge_->frameResults()) {
        spawnHitEffect(res.side, res.type,
                       static_cast<NoteType>(res.type), kDesignW, kDesignH);
    }

    // ---- 同步 note 判定状态到渲染层 ----
    // 头判/单判结果：记录到 note_judge_states_
    // 尾判结果由 JudgeEngine 直接处理，不需要额外追踪
    for (const auto& res : judge_->frameResults()) {
        if (!res.is_hold_tail) {
            // 检查是否已存在（避免同一 note 重复添加）
            bool exists = false;
            for (const auto& s : note_judge_states_) {
                if (s.note_id == res.note_id && !s.expired) { exists = true; break; }
            }
            if (!exists) {
                note_judge_states_.push_back({res.note_id, res.type, res.judged_at_ms, false});
            }
        }
    }

    // ---- 清理已过期的状态 ----
    note_judge_states_.erase(
        std::remove_if(note_judge_states_.begin(), note_judge_states_.end(),
            [](const NoteJudgeState& s) { return s.expired; }),
        note_judge_states_.end());

    // 谱面结束检测：所有 Note 都已判定且音频播放超过谱面时长 + 2s
    if (chart_ && audio_now_ms > static_cast<int64_t>(chart_->duration_ms) + 2000) {
        s_pending_stats = stats;
#if defined(__ANDROID__)
        // 初步 APK：谱面结束后直接退出
        transition_request_.type = Transition::POP;
#else
        transition_request_.type = Transition::REPLACE;
        transition_request_.target_scene_id = static_cast<int>(SceneID::RESULT);
#endif
    }
}

// ============================================================
// 渲染
// ============================================================

void SceneGameplay::render(RenderBatch& batch, int64_t audio_now_ms) {
    drawBackground(batch, kDesignW, kDesignH);
    drawTracks(batch, kDesignW, kDesignH);
    drawNotes(batch, kDesignW, kDesignH, audio_now_ms);
    drawHitEffects(batch, kDesignW, kDesignH, 1.0f / 60.0f);
    drawHUD(batch, kDesignW, kDesignH, audio_now_ms);
    drawJudgeEffects(batch);
}

/** 背景：歌曲封面暗化 50% + 底色兜底 */
void SceneGameplay::drawBackground(RenderBatch& batch, int screen_w, int screen_h) {
    if (cover_tex_ && cover_tex_->valid()) {
        // 绘制封面铺满全屏
        batch.submit(cover_tex_.get(), 0.0f, 0.0f,
                     static_cast<float>(screen_w), static_cast<float>(screen_h),
                     PackColor(255, 255, 255, 255));
        // 叠加 50% 黑色暗化遮罩
        batch.submitRect(0, 0, static_cast<float>(screen_w), static_cast<float>(screen_h),
                         PackColor(0, 0, 0, 128));
    } else {
        // 无封面时回退到纯色背景
        batch.submitRect(0, 0, static_cast<float>(screen_w), static_cast<float>(screen_h),
                         PackColor(15, 15, 15, 255));
    }
}

/** 三侧轨道：LEFT(水平) / DOWN(垂直) / RIGHT(水平)
 * 
 * 判定线位置（参考 DynaMaker/Dynamite 实际游戏）：
 *   LEFT:  在 side_w 处（左侧轨道右边界，青色竖线）
 *   RIGHT: 在 screen_w-side_w 处（右侧轨道左边界，青色竖线）
 *   DOWN:  在 judge_y 处（底部上方，青色横线）
 */
void SceneGameplay::drawTracks(RenderBatch& batch, int screen_w, int screen_h) {
    const float side_w = static_cast<float>(screen_w) * 0.25f;
    const float bottom_bar = static_cast<float>(screen_h) * 0.12f;
    const float judge_y = static_cast<float>(screen_h) - bottom_bar;
    const uint32_t track_bg = PackColor(35, 35, 35, 200);
    const uint32_t judge_line = PackColor(0, 217, 255, 200);
    const uint32_t border = PackColor(255, 215, 0, 120);

    // ---------- 左侧轨道（水平向左→右） ----------
    // 判定线在 side_w（右边界），竖线贯穿全高
    batch.submitRect(0.0f, 0.0f, side_w, static_cast<float>(screen_h), track_bg);
    batch.submitRect(side_w - 2.0f, 0.0f, 4.0f, static_cast<float>(screen_h), judge_line);
    batch.submitRect(0.0f, 0.0f, 1.0f, static_cast<float>(screen_h), border);

    // ---------- 右侧轨道（水平从右→左） ----------
    // 判定线在 screen_w-side_w（左边界），竖线贯穿全高
    float right_x = static_cast<float>(screen_w) - side_w;
    batch.submitRect(right_x, 0.0f, side_w, static_cast<float>(screen_h), track_bg);
    batch.submitRect(right_x - 2.0f, 0.0f, 4.0f, static_cast<float>(screen_h), judge_line);
    batch.submitRect(static_cast<float>(screen_w) - 1.0f, 0.0f, 1.0f, static_cast<float>(screen_h), border);

    // ---------- 下方 DOWN 轨道（垂直↓） ----------
    // 判定线在 judge_y（横线），轨道从顶部到 judge_y
    float down_x = side_w;
    float down_w = static_cast<float>(screen_w) - 2.0f * side_w;
    batch.submitRect(down_x, 0.0f, down_w, judge_y, track_bg);
    batch.submitRect(down_x, judge_y - 2.0f, down_w, 4.0f, judge_line);
    batch.submitRect(down_x, 0.0f, 1.0f, judge_y, border);
    batch.submitRect(down_x + down_w - 1.0f, 0.0f, 1.0f, judge_y, border);
}

/** Note 渲染 —— 三侧三方向（纹理标准化 + 旋转 + HOLD平铺）
 *
 * 纹理标准化方向：
 *   tap/slide (横向): 基准=DOWN（纹理宽→水平宽度，纹理高→垂直厚度）
 *   hold/hold_head (竖向): 基准=RIGHT（纹理宽→水平长度，纹理高→垂直宽度）
 *
 * 旋转规则：
 *   tap/slide（基准方向=DOWN）:
 *     DOWN:  rotation=0°（默认）
 *     RIGHT: rotation=-90°（顺时针90°：宽变垂直，高变水平）
 *     LEFT:  rotation=+90°（逆时针90°）
 *   hold/hold_head（基准方向=RIGHT）:
 *     RIGHT: rotation=0°（默认）
 *     LEFT:  rotation=180°（翻转）
 *     DOWN:  rotation=+90°（逆时针90°：宽变垂直(长度), 高变水平(宽度)）
 *
 * HOLD 平铺参考 DynaMaker drawLongNote：
 *   每段 180px，超长时循环平铺，最后一段不足时裁剪 UV
 */
void SceneGameplay::drawNotes(RenderBatch& batch, int screen_w, int screen_h,
                              int64_t audio_now_ms) {
    if (!chart_) return;

    const float side_w = static_cast<float>(screen_w) * 0.25f;
    const float bottom_bar = static_cast<float>(screen_h) * 0.12f;
    const float judge_y = static_cast<float>(screen_h) - bottom_bar;
    const float approach_time = 1500.0f / note_speed_;
    const float fall_range = judge_y;
    const uint32_t white = PackColor(255, 255, 255, 255);
    const float hold_seg_px = 180.0f;
    // 过线后降速时间：note 在此时间内从判定线缓动到屏幕边缘
    const float post_judge_slowdown_ms = 800.0f;
    // note 在屏幕边缘上方停留的额外比例（相对 fall_range）
    const float extra_dist_ratio = 0.30f;

    // 假身体颜色：半透明橙色
    const uint32_t ghost_color = PackColor(255, 200, 100, 80);

    for (const auto& note : chart_->notes) {
        float dt = static_cast<float>(note.time_ms - audio_now_ms);

        // 跳过 HOLD_BODY（XML 中无此类型，仅作安全兜底）
        if (note.type == NoteType::HOLD_BODY) continue;

        // ---- 可见性筛选 ----
        if (dt > approach_time) continue;

        // 查找判定状态
        auto judge_it = std::find_if(note_judge_states_.begin(), note_judge_states_.end(),
            [&](const NoteJudgeState& s) { return s.note_id == note.id && !s.expired; });
        bool is_judged = (judge_it != note_judge_states_.end());
        bool is_miss = is_judged && judge_it->result == JudgeType::MISS;

        // ---- 计算 t（下落进度）和位置 ----
        float t;
        if (dt > 0.0f) {
            // 过线前：正常速度
            t = 1.0f - (dt / approach_time);
            t = std::max(0.0f, t);
        } else {
            // 过线后：降速缓动
            float elapsed_after = -dt;
            float slowdown_ratio = elapsed_after / post_judge_slowdown_ms;
            slowdown_ratio = std::min(1.0f, slowdown_ratio);
            // ease-out quad：先快后慢
            float eased = slowdown_ratio * (2.0f - slowdown_ratio);
            // 过线后额外多走一小段距离使 note 刚好到达屏幕边缘
            float extra = extra_dist_ratio;
            t = 1.0f + eased * extra;
        }
        float dist = t * fall_range;

        // ---- 计算 alpha（淡出） ----
        float alpha = 1.0f;
        if (is_judged) {
            // 已判定：在 slowdown 时间内淡出
            float elapsed_since_judge = static_cast<float>(audio_now_ms - judge_it->judged_time_ms);
            if (elapsed_since_judge >= post_judge_slowdown_ms) {
                judge_it->expired = true;
                continue;  // 淡出结束
            }
            float fade_t = elapsed_since_judge / post_judge_slowdown_ms;
            alpha = 1.0f - fade_t * fade_t;  // ease-out 淡出
        } else if (dt < 0.0f) {
            // 过线后未判定：逐渐半透明，最多降到 40%
            float elapsed_after = -dt;
            float fade_t = std::min(1.0f, elapsed_after / post_judge_slowdown_ms);
            alpha = 1.0f - fade_t * 0.6f;
            alpha = std::max(0.4f, alpha);
        }

        uint32_t color = white;
        uint8_t a = static_cast<uint8_t>(255.0f * alpha);
        color = (white & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24);

        // ---- 选择纹理和尺寸 ----
        float nw = 60.0f, nh = 24.0f;
        const Texture* note_tex = nullptr;
        bool is_hold_style = false;  // true=hold/hold_head(基准RIGHT), false=tap/slide/zero(基准DOWN)
        float rotation = 0.0f;

        switch (note.type) {
            case NoteType::TAP:
            case NoteType::MULTI:
                note_tex = batch.getNoteTapTex();
                nw = 68.0f; nh = 24.0f;
                is_hold_style = false;
                break;
            case NoteType::SLIDE:
                note_tex = batch.getNoteSlideTex();
                nw = 72.0f; nh = 26.0f;
                is_hold_style = false;
                break;
            case NoteType::HOLD_HEAD:
                if (note.duration_ms == 0) {
                    note_tex = batch.getNoteHoldZeroTex();
                    nw = 40.0f; nh = 20.0f;
                    is_hold_style = false;
                } else {
                    note_tex = batch.getNoteHoldHeadTex();
                    nw = 36.0f; nh = 28.0f;
                    is_hold_style = true;
                }
                break;
            case NoteType::HOLD_TAIL:
                if (note.duration_ms == 0) continue;
                note_tex = batch.getNoteHoldHeadTex();
                nw = 34.0f; nh = 26.0f;
                is_hold_style = true;
                break;
            default:
                break;
        }

        // ---- 根据轨道方向计算位置和旋转 ----
        float pos_x = 0.0f, pos_y = 0.0f;
        float draw_w = nw, draw_h = nh;

        if (!is_hold_style) {
            switch (note.side) {
                case SideType::DOWN:
                    pos_x = static_cast<float>(screen_w) * 0.5f - nw * 0.5f;
                    pos_y = dist - nh * 0.5f;
                    rotation = 0.0f;
                    draw_w = nw; draw_h = nh;
                    break;
                case SideType::LEFT:
                    pos_x = -nw * 0.5f + (side_w + nw) * t;
                    pos_y = judge_y * 0.45f + (note.position - 0.5f) * judge_y * 0.6f - nh * 0.5f;
                    rotation = 1.57079633f;
                    draw_w = nh; draw_h = nw;
                    break;
                case SideType::RIGHT:
                    pos_x = (static_cast<float>(screen_w) + nw * 0.5f) - (side_w + nw) * t;
                    pos_y = judge_y * 0.45f + (note.position - 0.5f) * judge_y * 0.6f - nh * 0.5f;
                    rotation = -1.57079633f;
                    draw_w = nh; draw_h = nw;
                    break;
            }
        } else {
            switch (note.side) {
                case SideType::RIGHT:
                    pos_x = (static_cast<float>(screen_w) + nw * 0.5f) - (side_w + nw) * t;
                    pos_y = judge_y * 0.45f + (note.position - 0.5f) * judge_y * 0.6f - nh * 0.5f;
                    rotation = 0.0f;
                    draw_w = nw; draw_h = nh;
                    break;
                case SideType::LEFT:
                    pos_x = -nw * 0.5f + (side_w + nw) * t;
                    pos_y = judge_y * 0.45f + (note.position - 0.5f) * judge_y * 0.6f - nh * 0.5f;
                    rotation = 3.14159265f;
                    draw_w = nw; draw_h = nh;
                    break;
                case SideType::DOWN:
                    pos_x = static_cast<float>(screen_w) * 0.5f + (note.position - 0.5f) * side_w * 0.8f - nh * 0.5f;
                    pos_y = dist - nw * 0.5f;
                    rotation = 1.57079633f;
                    draw_w = nh; draw_h = nw;
                    break;
            }
        }

                // ---- HOLD 身体：head->假身体->判定线->真身体->tail ----
        if (note.type == NoteType::HOLD_HEAD && note.duration_ms > 0) {
            const Texture* hold_tex = batch.getNoteHoldTex();
            if (hold_tex && hold_tex->valid()) {
                float dur_s = static_cast<float>(note.duration_ms) / 1000.0f;
                float full_len = dur_s * note_speed_ * fall_range / (approach_time / 1000.0f);
                full_len = std::max(20.0f, full_len);

                // ---- 计算轴坐标 ----
                // head_axis <= judge_axis <= tail_axis (DOWN/RIGHT)
                // tail_axis <= judge_axis <= head_axis (LEFT)
                float head_axis, judge_axis;
                bool is_vertical;

                if (note.side == SideType::DOWN) {
                    head_axis = pos_y + draw_h * 0.5f;
                    judge_axis = judge_y;
                    is_vertical = true;
                } else if (note.side == SideType::RIGHT) {
                    head_axis = pos_x + draw_w * 0.5f;
                    judge_axis = static_cast<float>(screen_w) - side_w;
                    is_vertical = false;
                } else {  // LEFT
                    head_axis = pos_x + draw_w * 0.5f;
                    judge_axis = side_w;
                    is_vertical = false;
                }

                // 假身体：head_axis 到 judge_axis
                float ghost_len = is_vertical || note.side == SideType::RIGHT
                    ? std::max(0.0f, judge_axis - head_axis)
                    : std::max(0.0f, head_axis - judge_axis);
                ghost_len = std::min(ghost_len, full_len);

                // 真身体：judge_axis 往后延伸 full_len - ghost_len
                float real_len = full_len - ghost_len;

                // 真身体收起：head 过线后逐渐缩短
                if (dt < 0.0f && real_len > 0.0f) {
                    float elapsed_after = -dt;
                    float shrink_speed = note_speed_ * fall_range / (approach_time / 1000.0f);
                    float shrunk = elapsed_after * shrink_speed;
                    shrunk = std::min(shrunk, real_len);
                    real_len -= shrunk;
                }

                // ---- 渲染假身体（半透明橙色，跟随 head alpha） ----
                if (ghost_len > 4.0f) {
                    uint32_t ghost_clr = PackColor(255, 200, 100,
                        static_cast<uint8_t>(static_cast<float>(a) * 0.3f));

                    if (note.side == SideType::DOWN) {
                        float seg_y = pos_y + draw_h;
                        float remain = ghost_len;
                        while (remain > 0.0f) {
                            float seg_len = std::min(remain, hold_seg_px);
                            float uv_start = 0.0f;
                            if (seg_len < hold_seg_px) uv_start = 1.0f - seg_len / hold_seg_px;
                            float uv_len = seg_len / hold_seg_px;
                            batch.submit(hold_tex, pos_x, seg_y, draw_w, seg_len,
                                         ghost_clr, rotation, 0.0f, uv_start, 1.0f, uv_len);
                            seg_y += seg_len;
                            remain -= seg_len;
                        }
                    } else if (note.side == SideType::RIGHT) {
                        float seg_x = pos_x + draw_w;
                        float remain = ghost_len;
                        while (remain > 0.0f) {
                            float seg_len = std::min(remain, hold_seg_px);
                            float uv_start = (seg_len < hold_seg_px) ? 1.0f - seg_len / hold_seg_px : 0.0f;
                            float uv_len = seg_len / hold_seg_px;
                            batch.submit(hold_tex, seg_x, pos_y, seg_len, draw_h,
                                         ghost_clr, rotation, uv_start, 0.0f, uv_len, 1.0f);
                            seg_x += seg_len;
                            remain -= seg_len;
                        }
                    } else if (note.side == SideType::LEFT) {
                        float seg_x = pos_x - ghost_len;
                        float remain = ghost_len;
                        while (remain > 0.0f) {
                            float seg_len = std::min(remain, hold_seg_px);
                            float uv_start = (seg_len < hold_seg_px) ? 1.0f - seg_len / hold_seg_px : 0.0f;
                            float uv_len = seg_len / hold_seg_px;
                            batch.submit(hold_tex, seg_x, pos_y, seg_len, draw_h,
                                         ghost_clr, rotation, uv_start, 0.0f, uv_len, 1.0f);
                            seg_x += seg_len;
                            remain -= seg_len;
                        }
                    }
                }

                // ---- 渲染真身体（正常颜色，从判定线向下延伸） ----
                if (real_len > 4.0f) {
                    if (note.side == SideType::DOWN) {
                        float seg_y = judge_y;
                        float remain = real_len;
                        while (remain > 0.0f) {
                            float seg_len = std::min(remain, hold_seg_px);
                            float uv_start = 0.0f;
                            if (seg_len < hold_seg_px) uv_start = 1.0f - seg_len / hold_seg_px;
                            float uv_len = seg_len / hold_seg_px;
                            batch.submit(hold_tex, pos_x, seg_y, draw_w, seg_len,
                                         color, rotation, 0.0f, uv_start, 1.0f, uv_len);
                            seg_y += seg_len;
                            remain -= seg_len;
                        }
                    } else if (note.side == SideType::RIGHT) {
                        float seg_x = static_cast<float>(screen_w) - side_w;
                        float remain = real_len;
                        while (remain > 0.0f) {
                            float seg_len = std::min(remain, hold_seg_px);
                            float uv_start = (seg_len < hold_seg_px) ? 1.0f - seg_len / hold_seg_px : 0.0f;
                            float uv_len = seg_len / hold_seg_px;
                            batch.submit(hold_tex, seg_x, pos_y, seg_len, draw_h,
                                         color, rotation, uv_start, 0.0f, uv_len, 1.0f);
                            seg_x += seg_len;
                            remain -= seg_len;
                        }
                    } else if (note.side == SideType::LEFT) {
                        float seg_x = side_w - real_len;
                        float remain = real_len;
                        while (remain > 0.0f) {
                            float seg_len = std::min(remain, hold_seg_px);
                            float uv_start = (seg_len < hold_seg_px) ? 1.0f - seg_len / hold_seg_px : 0.0f;
                            float uv_len = seg_len / hold_seg_px;
                            batch.submit(hold_tex, seg_x, pos_y, seg_len, draw_h,
                                         color, rotation, uv_start, 0.0f, uv_len, 1.0f);
                            seg_x += seg_len;
                            remain -= seg_len;
                        }
                    }
                }
            }
        }

        // ---- 提交 note 本体 ----
        if (note_tex && note_tex->valid()) {
            batch.submit(note_tex, pos_x, pos_y, draw_w, draw_h, color, rotation);
        } else {
            batch.submitRect(pos_x, pos_y, draw_w, draw_h, color);
        }
    }
}
void SceneGameplay::drawHUD(RenderBatch& batch, int screen_w, int screen_h,
                              int64_t audio_now_ms) {

    const float margin = 24.0f;
    const uint32_t text_white = PackColor(255, 255, 255, 255);

        // ---------- 顶部 HUD 统计条（带向左滚动的 45° 棋盘格斜纹背景） ----------
    float top_y = margin;
    {
        float top_bar_h = 48.0f;
        batch.submitRect(0.0f, 0.0f, static_cast<float>(screen_w), top_bar_h,
                         PackColor(15, 15, 15, 200));
        float top_offset = -static_cast<float>(audio_now_ms) * 0.05f;
        batch.submitStripedRect(0.0f, 0.0f, static_cast<float>(screen_w), top_bar_h,
                                PackColor(15, 15, 15, 0),
                                PackColor(30, 30, 30, 64),
                                -1,
                                top_offset);
    }
    float tag_h = 32.0f;
    float tag_gap = 12.0f;
    float tag_x = margin;

    // 统计标签底色 + 数值
    auto drawStatTag = [&](const char* label, int value, uint32_t bg_color,
                           uint32_t txt_color, float x, float y) {
        float label_w = 80.0f;
        batch.submitRoundedRect(x, y, label_w + 60.0f, tag_h, 4.0f, bg_color);
        batch.submitText(label, x + 6.0f, y + 5.0f, 0.6f, txt_color);
        batch.submitText(std::to_string(value), x + label_w, y + 2.0f, 0.8f, text_white);
    };

    drawStatTag("PERFECT", last_perfect_, PackColor(0, 200, 255, 180),
                PackColor(0, 255, 200, 255), tag_x, top_y);
    tag_x += 150.0f + tag_gap;
    drawStatTag("GOOD", last_good_, PackColor(255, 170, 0, 180),
                PackColor(255, 200, 0, 255), tag_x, top_y);
    tag_x += 150.0f + tag_gap;
    drawStatTag("MISS", last_miss_, PackColor(255, 68, 68, 180),
                PackColor(255, 100, 100, 255), tag_x, top_y);

    // 准确率（百分比数字 + 进度条）
    float acc = 0.0f;
    int total = last_perfect_ + last_good_ + last_miss_;
    if (total > 0) {
        acc = (last_perfect_ * 100.0f + last_good_ * 50.0f) / total;
    }
    char acc_str[32];
    std::snprintf(acc_str, sizeof(acc_str), "%.2f%%", acc);
    float acc_x = static_cast<float>(screen_w) - margin - 180.0f;
    batch.submitText("ACC", acc_x, top_y + 2.0f, 0.7f, PackColor(255, 215, 0, 255));
    batch.submitText(acc_str, acc_x + 50.0f, top_y, 0.9f, PackColor(255, 215, 0, 255));

    // ---------- 左侧 Combo ----------
    float left_x = margin;
    float combo_y = static_cast<float>(screen_h) * 0.35f;
    if (last_combo_ > 0) {
        batch.submitText("COMBO", left_x, combo_y, 0.7f, PackColor(255, 255, 255, 180));
        batch.submitText(std::to_string(last_combo_), left_x, combo_y + 40.0f,
                         1.4f, PackColor(255, 215, 0, 255));
    }

    // ---------- 中央暂停按钮（⏸ 简化表示） ----------
    float pause_x = static_cast<float>(screen_w) * 0.5f - 20.0f;
    float pause_y = static_cast<float>(screen_h) * 0.15f;
    batch.submitRect(pause_x, pause_y, 6.0f, 28.0f, PackColor(255, 255, 255, 200));
    batch.submitRect(pause_x + 14.0f, pause_y, 6.0f, 28.0f, PackColor(255, 255, 255, 200));

    // ---------- 右侧分数（7 位等宽） ----------
    float right_x = static_cast<float>(screen_w) - margin - 260.0f;
    float score_y = static_cast<float>(screen_h) * 0.22f;
    batch.submitText("SCORE", right_x + 60.0f, score_y, 0.7f, PackColor(255, 255, 255, 180));
    char score_str[16];
    std::snprintf(score_str, sizeof(score_str), "%07d", static_cast<int>(score_display_));
    batch.submitText(score_str, right_x, score_y + 40.0f, 1.4f, text_white);

        // ---------- 底部信息条（带向右滚动的 45° 棋盘格斜纹） ----------
    float info_h = 80.0f;
    float info_y = static_cast<float>(screen_h) - info_h;
    batch.submitRect(0.0f, info_y, static_cast<float>(screen_w), info_h,
                     PackColor(20, 20, 20, 220));
    // 斜纹覆盖（方向=1：向右滚动）
    float offset = static_cast<float>(audio_now_ms) * 0.05f;
    batch.submitStripedRect(0.0f, info_y, static_cast<float>(screen_w), info_h,
                            PackColor(20, 20, 20, 0),       // base_color=透明
                            PackColor(40, 40, 40, 64),      // stripe_color=浅灰半透明
                            1,                               // direction=1: 向右（底部）
                            offset);

    if (chart_) {
        // 曲名
        batch.submitText(s_pending_song_title, margin, info_y + 20.0f,
                         0.9f, text_white);
        // 难度标签（按规范配色）
        uint32_t diff_color = PackColor(200, 200, 200, 255);
        if (s_pending_difficulty == "CASUAL") diff_color = PackColor(68,  170, 68,  255);
        else if (s_pending_difficulty == "NORMAL") diff_color = PackColor(68,  136, 255, 255);
        else if (s_pending_difficulty == "HARD")   diff_color = PackColor(255, 68,  68,  255);
        else if (s_pending_difficulty == "MEGA")   diff_color = PackColor(255, 0,   160, 255);
        else if (s_pending_difficulty == "GIGA")   diff_color = PackColor(170, 68,  255, 255);
        else if (s_pending_difficulty == "TERA")   diff_color = PackColor(255, 170, 0,   255);

        float tag_w = 160.0f;
        float tag_h_btn = 44.0f;
        float tag_x_pos = static_cast<float>(screen_w) * 0.6f;
        batch.submitRoundedRect(tag_x_pos, info_y + 18.0f, tag_w, tag_h_btn, 8.0f, diff_color);
        batch.submitText(s_pending_difficulty, tag_x_pos + 20.0f, info_y + 24.0f,
                         1.0f, text_white);
    }
}

/** 判定特效：在对应轨道的判定线位置显示 */
void SceneGameplay::drawJudgeEffects(RenderBatch& batch) {
    if (!judge_) return;

    const float side_w = kDesignW * 0.25f;
    const float bottom_bar = kDesignH * 0.12f;
    const float judge_y = kDesignH - bottom_bar;

    for (const auto& res : judge_->frameResults()) {
        uint32_t flash_color;
        switch (res.type) {
            case JudgeType::PERFECT: flash_color = PackColor(0, 255, 200, 180); break;
            case JudgeType::GOOD:    flash_color = PackColor(255, 200, 0, 180); break;
            case JudgeType::MISS:    flash_color = PackColor(255, 50, 50, 180); break;
            default:                 flash_color = PackColor(255, 255, 255, 0); break;
        }

        float fx = 0.0f, fy = 0.0f;
        float fw = 100.0f, fh = 80.0f;

        switch (res.side) {
            case SideType::LEFT:
                // LEFT 判定线在 side_w（右边界），特效在判定线处
                fx = side_w - fw * 0.5f;
                fy = judge_y * 0.45f - fh * 0.5f;
                break;
            case SideType::DOWN:
                // DOWN 判定线在 judge_y（底部横线）
                fx = kDesignW * 0.5f - fw * 0.5f;
                fy = judge_y - fh * 0.5f;
                break;
            case SideType::RIGHT:
                // RIGHT 判定线在 kDesignW-side_w（左边界）
                fx = kDesignW - side_w - fw * 0.5f;
                fy = judge_y * 0.45f - fh * 0.5f;
                break;
        }

        batch.submitRect(fx, fy, fw, fh, flash_color);
    }
}

/** 生成打击闪光特效（在对应轨道判定线位置） */
void SceneGameplay::spawnHitEffect(SideType side, JudgeType type,
                                    NoteType note_type,
                                    int screen_w, int screen_h) {
    const float side_w = static_cast<float>(screen_w) * 0.25f;
    const float bottom_bar = static_cast<float>(screen_h) * 0.12f;
    const float judge_y = static_cast<float>(screen_h) - bottom_bar;

    HitEffect eff{};
    switch (note_type) {
        case NoteType::TAP:
        case NoteType::MULTI:
            eff.width = side_w * 0.45f; break;
        case NoteType::SLIDE:
            eff.width = side_w * 0.50f; break;
        case NoteType::HOLD_HEAD:
        case NoteType::HOLD_BODY:
        case NoteType::HOLD_TAIL:
            eff.width = side_w * 0.35f; break;
        default:
            eff.width = side_w * 0.4f; break;
    }
    eff.height = 8.0f;
    eff.max_lifetime = 0.12f;
    eff.lifetime = eff.max_lifetime;
    eff.note_type = note_type;

    switch (type) {
        case JudgeType::PERFECT: eff.color = PackColor(255, 255, 255, 255); break;
        case JudgeType::GOOD:    eff.color = PackColor(255, 220, 100, 255); break;
        case JudgeType::MISS:    eff.color = PackColor(255, 50,  50,  255); break;
    }

    // 特效位置：在对应轨道的判定线处
    switch (side) {
        case SideType::LEFT:
            eff.x = side_w - eff.width * 0.5f;
            eff.y = judge_y * 0.45f - eff.height * 0.5f;
            break;
        case SideType::DOWN:
            eff.x = static_cast<float>(screen_w) * 0.5f - eff.width * 0.5f;
            eff.y = judge_y - eff.height * 0.5f;
            break;
        case SideType::RIGHT:
            eff.x = static_cast<float>(screen_w) - side_w - eff.width * 0.5f;
            eff.y = judge_y * 0.45f - eff.height * 0.5f;
            break;
    }

    hit_effects_.push_back(eff);
}

/** 绘制并更新打击特效（使用 Light 系列纹理） */
void SceneGameplay::drawHitEffects(RenderBatch& batch, int screen_w, int screen_h,
                                   float dt, int64_t audio_now_ms) {
    (void)screen_w;
    (void)screen_h;
    (void)audio_now_ms;

    // 更新生命周期
    for (auto it = hit_effects_.begin(); it != hit_effects_.end();) {
        it->lifetime -= dt;
        if (it->lifetime <= 0.0f) {
            it = hit_effects_.erase(it);
        } else {
            ++it;
        }
    }

    // 绘制存活中的特效
    for (auto& eff : hit_effects_) {
        float t = eff.lifetime / eff.max_lifetime; // 1.0 -> 0.0
        float scale = 1.0f + (1.0f - t) * 1.0f;    // 逐渐扩大
        uint8_t alpha = static_cast<uint8_t>(255.0f * t);
        uint32_t color = (eff.color & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha) << 24);

        float w = eff.width * scale;
        float h = eff.height * scale;
        float x = eff.x + eff.width * 0.5f - w * 0.5f;
        float y = eff.y + eff.height * 0.5f - h * 0.5f;

        // 根据 note 类型选择对应 Light 特效纹理
        std::string eff_name;
        switch (eff.note_type) {
            case NoteType::TAP:
            case NoteType::MULTI:
                eff_name = "tap"; break;
            case NoteType::HOLD_HEAD:
            case NoteType::HOLD_BODY:
            case NoteType::HOLD_TAIL:
                eff_name = "hold"; break;
            case NoteType::SLIDE:
                eff_name = "slide"; break;
            default:
                eff_name = "tap"; break;
        }
        // 根据生命周期选择序列帧（0~3）
        int frame = static_cast<int>((1.0f - t) * 4.0f);
        frame = std::min(3, std::max(0, frame));
        const Texture* tex = batch.getEffectTex(eff_name, frame);
        if (tex && tex->valid()) {
            batch.submit(tex, x, y, w, h, color);
        } else {
            batch.submitRect(x, y, w, h, color);
        }
    }
}

// ============================================================
// 输入处理
// ============================================================

void SceneGameplay::handleInput(const std::vector<RawTouch>& touches,
                                int64_t audio_now_ms) {
    (void)audio_now_ms;
    s_current_touches = touches;

    // 检测返回手势（点击屏幕左上角区域）
    for (const auto& t : touches) {
        if (t.is_new && t.is_down) {
            if (t.x < 0.1f && t.y < 0.1f) {
                transition_request_.type = Transition::POP;
                return;
            }
        }
    }
}
