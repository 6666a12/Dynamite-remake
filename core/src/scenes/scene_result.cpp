/**
 * 结算场景实现 —— 分数动画 + 评级 + 统计面板
 * 
 * 视觉设计：
 * - 背景：#0F0F0F（与整体 UI 一致）
 * - 分数大数字：居中，ease-out 动画从 0 滚动到目标值（1.5 秒）
 * - 评级（S/A/B/C）：根据准确率确定，放大显示
 * - 统计面板：Perfect / Good / Miss / Max Combo 四项横向排列
 * - 底部按钮：返回（POP）和重试（REPLACE -> GAMEPLAY）
 */

#include "scenes/scene_result.h"
#include "engine/render_batch.h"
#include "engine/input_manager.h"
#include "engine/judge_engine.h"
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
// 跨场景共享数据声明（定义在 scene_gameplay.cpp）
// ============================================================

extern JudgeEngine::Stats s_pending_stats;
extern std::string s_pending_song_title;
extern std::string s_pending_difficulty;

// ============================================================
// 生命周期
// ============================================================

void SceneResult::init() {
    // 初始化时尝试接收来自游玩场景的结算数据
    if (s_pending_stats.max_combo > 0 || s_pending_stats.score > 0) {
        stats_ = s_pending_stats;
        song_title_ = s_pending_song_title;
        difficulty_ = s_pending_difficulty;
    }

    anim_score_ = 0.0f;
    anim_timer_ = 0.0f;
    anim_done_ = false;
}

void SceneResult::enter() {
    // 每次进入结算页时重置动画
    anim_score_ = 0.0f;
    anim_timer_ = 0.0f;
    anim_done_ = false;
}

void SceneResult::update(int64_t audio_now_ms) {
    (void)audio_now_ms;
    if (anim_done_) {
        return;
    }

    // 假设 60fps，每帧约 16.7ms
    const float dt = 1.0f / 60.0f;
    anim_timer_ += dt;

    const float duration = 1.5f;  // 动画总时长 1.5 秒
    if (anim_timer_ >= duration) {
        anim_timer_ = duration;
        anim_done_ = true;
    }

    // Quadratic ease-out：1 - (1 - t)^2
    float t = anim_timer_ / duration;
    float ease = 1.0f - (1.0f - t) * (1.0f - t);
    anim_score_ = static_cast<float>(stats_.score) * ease;
}

// ============================================================
// 评级计算
// ============================================================

std::string SceneResult::getRating() const {
    if (stats_.is_all_perfect) {
        return "S";
    }
    if (stats_.accuracy >= 0.95) return "S";
    if (stats_.accuracy >= 0.90) return "A";
    if (stats_.accuracy >= 0.80) return "B";
    if (stats_.accuracy >= 0.70) return "C";
    return "D";
}

// ============================================================
// 渲染
// ============================================================

void SceneResult::render(RenderBatch& batch, int64_t audio_now_ms) {
    (void)audio_now_ms;

    // 全屏深色背景
    batch.submitRect(0.0f, 0.0f, kDesignW, kDesignH, PackColor(15, 15, 15, 255));

        // ---------- 顶部标题 ----------
    batch.submitText("RESULT", kDesignW * 0.5f - 120.0f, 40.0f, 1.5f,
                     PackColor(255, 255, 255, 255));

    // ---------- 分数大数字（带动画） ----------
    float score_y = 140.0f;
    int display_score = static_cast<int>(anim_score_);
    batch.submitText(std::to_string(display_score),
                     kDesignW * 0.5f - 180.0f, score_y, 2.0f,
                     PackColor(255, 255, 255, 255));

    // ---------- 评级 ----------
    std::string rating = getRating();
    uint32_t rating_color;
    if (rating == "S") rating_color = PackColor(255, 215, 0, 255);     // 金黄
    else if (rating == "A") rating_color = PackColor(192, 192, 192, 255); // 银
    else if (rating == "B") rating_color = PackColor(205, 127, 50, 255);  // 铜
    else if (rating == "C") rating_color = PackColor(150, 150, 150, 255); // 灰
    else rating_color = PackColor(100, 100, 100, 255);                    // 深灰

        float rating_y = score_y + 100.0f;
    // 评级背景圆角矩形
    batch.submitRoundedRect(kDesignW * 0.5f - 100.0f, rating_y, 200.0f, 100.0f,
                            20.0f, PackColor(40, 40, 40, 255));
    batch.submitText(rating, kDesignW * 0.5f - 50.0f, rating_y + 10.0f,
                     3.0f, rating_color);

    // ---------- 统计面板 ----------
    drawStatsPanel(batch, kDesignW, kDesignH);

        // ---------- 底部按钮 ----------
    float btn_y = kDesignH - 140.0f;
    float btn_w = 280.0f;
    float btn_h = 90.0f;
    uint32_t btn_bg = PackColor(45, 45, 45, 255);
    uint32_t btn_border = PackColor(80, 80, 80, 255);

    // 返回按钮（左侧）时，先指向 MAIN_MENU
    batch.submitRoundedRect(kDesignW * 0.25f - btn_w * 0.5f, btn_y,
                            btn_w, btn_h, 12.0f, btn_bg);
    batch.submitRect(kDesignW * 0.25f - btn_w * 0.5f, btn_y,
                     btn_w, 2.0f, btn_border);  // 上边框线
    batch.submitText("BACK", kDesignW * 0.25f - 60.0f, btn_y + 20.0f,
                     1.2f, PackColor(255, 255, 255, 255));

    // 重试按钮（右侧）
    batch.submitRoundedRect(kDesignW * 0.75f - btn_w * 0.5f, btn_y,
                            btn_w, btn_h, 12.0f, btn_bg);
    batch.submitRect(kDesignW * 0.75f - btn_w * 0.5f, btn_y,
                     btn_w, 2.0f, PackColor(0, 200, 255, 255));  // 强调色上边框
    batch.submitText("RETRY", kDesignW * 0.75f - 70.0f, btn_y + 20.0f,
                     1.2f, PackColor(0, 200, 255, 255));
}

/** 统计面板：Perfect / Good / Miss / Max Combo */
void SceneResult::drawStatsPanel(RenderBatch& batch, int screen_w, int screen_h) {
    (void)screen_h;

        const float panel_y = 380.0f;
    const float panel_h = 200.0f;
    const float margin = 60.0f;

    // 面板背景
    batch.submitRoundedRect(margin, panel_y,
                            static_cast<float>(screen_w) - 2.0f * margin,
                            panel_h, 16.0f, PackColor(28, 28, 28, 255));

    // 四条统计项等分横向空间
    float item_w = (static_cast<float>(screen_w) - 2.0f * margin) / 4.0f;
    float item_x = margin;
        float label_y = panel_y + 30.0f;
    float value_y = panel_y + 80.0f;

    struct Item { const char* label; int value; uint32_t color; };
    Item items[4] = {
        {"PERFECT",   stats_.perfect,   PackColor(0,   255, 200, 255)},
        {"GOOD",      stats_.good,      PackColor(68,  255, 136, 255)},
        {"MISS",      stats_.miss,      PackColor(255, 68,  68,  255)},
        {"MAX COMBO", stats_.max_combo, PackColor(68,  136, 255, 255)},
    };

    for (int i = 0; i < 4; ++i) {
        batch.submitText(items[i].label, item_x + 20.0f, label_y, 0.8f,
                         PackColor(180, 180, 180, 255));
        batch.submitText(std::to_string(items[i].value),
                         item_x + 20.0f, value_y, 1.3f, items[i].color);

        // 项间分隔线
        if (i < 3) {
            batch.submitRect(item_x + item_w - 1.0f, panel_y + 30.0f,
                             2.0f, panel_h - 60.0f, PackColor(50, 50, 50, 255));
        }
        item_x += item_w;
    }

        // 准确率单独一行显示
    float acc_y = panel_y + 140.0f;
    char acc_str[32];
    std::snprintf(acc_str, sizeof(acc_str), "ACCURACY: %.2f%%", stats_.accuracy);
    batch.submitText(acc_str, margin + 30.0f, acc_y, 0.9f,
                     PackColor(255, 255, 255, 255));
}

// ============================================================
// 输入处理
// ============================================================

void SceneResult::handleInput(const std::vector<RawTouch>& touches,
                              int64_t audio_now_ms) {
    (void)audio_now_ms;

        float btn_y = kDesignH - 140.0f;
    float btn_w = 280.0f;
    float btn_h = 90.0f;

    for (const auto& t : touches) {
        if (!t.is_new || !t.is_down) {
            continue;
        }

        float px = t.x * kDesignW;
        float py = t.y * kDesignH;

                // 返回按钮区域检测（左侧）-> 回到主菜单
        float back_left = kDesignW * 0.25f - btn_w * 0.5f;
        float back_top  = btn_y;
        if (px >= back_left && px <= back_left + btn_w &&
            py >= back_top  && py <= back_top + btn_h) {
            transition_request_.type = Transition::PUSH;
            transition_request_.target_scene_id = static_cast<int>(SceneID::MAIN_MENU);
            return;
        }

        // 重试按钮区域检测（右侧）
        float retry_left = kDesignW * 0.75f - btn_w * 0.5f;
        float retry_top  = btn_y;
        if (px >= retry_left && px <= retry_left + btn_w &&
            py >= retry_top  && py <= retry_top + btn_h) {
            transition_request_.type = Transition::REPLACE;
            transition_request_.target_scene_id = static_cast<int>(SceneID::GAMEPLAY);
            return;
        }
    }
}

// ============================================================
// 外部设置接口
// ============================================================

void SceneResult::setChartInfo(const std::string& title,
                               const std::string& difficulty,
                               int level) {
    song_title_ = title;
    difficulty_ = difficulty;
    level_ = level;
}
