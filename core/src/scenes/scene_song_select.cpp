/**
 * 选歌场景实现 —— 歌曲封面卡片网格 + 顶部状态栏 + 底部筛选栏
 * 
 * 视觉风格：
 * - 背景：#0F0F0F（极深灰黑）
 * - 卡片：#1A1A1A（深灰），带轻微边框
 * - 难度标签：按 Dynamite 官方配色区分
 * 
 * 交互：
 * - 垂直滑动浏览歌曲列表
 * - 点击卡片进入游玩场景
 */

#include "scenes/scene_song_select.h"
#include "engine/render_batch.h"
#include "engine/input_manager.h"
#include "utils/logger.h"
#include <cmath>
#include <unordered_map>

// 设计分辨率（横屏 16:9）
static constexpr int kDesignW = 1920;
static constexpr int kDesignH = 1080;

/** 全局斜纹滚动时间（毫秒），由 update 实时更新 */
static int64_t g_stripe_time_ms_ = 0;

/** 将 RGBA 四分量打包为 uint32_t */
static inline uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r)
         | (static_cast<uint32_t>(g) << 8)
         | (static_cast<uint32_t>(b) << 16)
         | (static_cast<uint32_t>(a) << 24);
}

/** 根据难度字符串返回对应标签色（Dynamite 官方配色） */
static uint32_t DifficultyColor(const std::string& diff) {
    if (diff == "CASUAL") return PackColor(68,  170, 68,  255);  // 绿  #44AA44
    if (diff == "NORMAL") return PackColor(68,  136, 255, 255);  // 蓝  #4488FF
    if (diff == "HARD")   return PackColor(255, 68,  68,  255);  // 红  #FF4444
    if (diff == "MEGA")   return PackColor(255, 0,   160, 255);  // 品红 #FF00A0
    if (diff == "GIGA")   return PackColor(170, 68,  255, 255);  // 紫  #AA44FF
    if (diff == "TERA")   return PackColor(255, 170, 0,   255);  // 橙  #FFAA00
    return PackColor(200, 200, 200, 255);  // 默认灰
}

// ============================================================
// 场景生命周期
// ============================================================

void SceneSongSelect::init() {
    loadSongs();
}

void SceneSongSelect::enter() {
    scroll_y_ = 0.0f;
    target_scroll_y_ = 0.0f;
}

void SceneSongSelect::update(int64_t audio_now_ms) {
    (void)audio_now_ms;
    // 平滑滚动插值：每帧向目标值靠近 20%
    scroll_y_ += (target_scroll_y_ - scroll_y_) * 0.2f;

    // 更新斜纹滚动时间
    g_stripe_time_ms_ = audio_now_ms;
}

// ============================================================
// 歌曲数据加载（Mock）
// ============================================================

void SceneSongSelect::loadSongs() {
    songs_.clear();

    // mock 歌曲 1
    {
        SongCard s;
        s.id = "dyn_001";
        s.title = "Neon Pulse";
        s.artist = "CyberBeats";
        s.cover_path = "assets/covers/dyn_001.jpg";
        s.difficulties = {
            {"CASUAL", 3},
            {"NORMAL", 6},
            {"HARD", 10},
            {"MEGA", 14}
        };
        songs_.push_back(std::move(s));
    }

    // mock 歌曲 2
    {
        SongCard s;
        s.id = "dyn_002";
        s.title = "Stardust Memory";
        s.artist = "LunaProject";
        s.cover_path = "assets/covers/dyn_002.jpg";
        s.difficulties = {
            {"NORMAL", 5},
            {"HARD", 9},
            {"GIGA", 15}
        };
        songs_.push_back(std::move(s));
    }

    // mock 歌曲 3
    {
        SongCard s;
        s.id = "dyn_003";
        s.title = "Terminal Velocity";
        s.artist = "NullPointer";
        s.cover_path = "assets/covers/dyn_003.jpg";
        s.difficulties = {
            {"CASUAL", 4},
            {"HARD", 11},
            {"MEGA", 13},
            {"TERA", 16}
        };
        songs_.push_back(std::move(s));
    }

    // mock 歌曲 4（用于测试多行滚动）
    {
        SongCard s;
        s.id = "dyn_004";
        s.title = "Quantum Leap";
        s.artist = "Entangled";
        s.cover_path = "assets/covers/dyn_004.jpg";
        s.difficulties = {
            {"HARD", 8},
            {"MEGA", 12}
        };
        songs_.push_back(std::move(s));
    }
}

// ============================================================
// 渲染
// ============================================================

void SceneSongSelect::render(RenderBatch& batch, int64_t audio_now_ms) {
    (void)audio_now_ms;

    // 全屏背景
    batch.submitRect(0, 0, kDesignW, kDesignH, PackColor(15, 15, 15, 255));

    drawHeader(batch);
    drawSongGrid(batch, kDesignW, kDesignH);
    drawBottomBar(batch, kDesignW, kDesignH);
}

/** 顶部状态栏：56dp 高度，45°斜纹背景（斜纹向左滚动） */
void SceneSongSelect::drawHeader(RenderBatch& batch) {
    const float h = 56.0f;
    // 状态栏底色
    batch.submitRect(0, 0, kDesignW, h, PackColor(15, 15, 15, 255));
    // 45° 棋盘格斜纹覆盖（方向=-1：向左移动）
    // offset 随时间变化，驱动斜纹向左滚动
    float offset = -static_cast<float>(g_stripe_time_ms_) * 0.05f;  // 速度：每1000ms 偏移50px
    batch.submitStripedRect(0, 0, static_cast<float>(kDesignW), h,
                            PackColor(15, 15, 15, 0),       // base_color=透明（显示下方底色）
                            PackColor(30, 30, 30, 77),      // stripe_color=深灰半透明
                            -1,                              // direction=-1: 向左
                            offset);

    // 标题居中
    batch.submitText("SONG SELECT", kDesignW * 0.5f - 120.0f, 10.0f, 1.0f,
                     PackColor(255, 255, 255, 255));

    // 右上角返回按钮（点击回到主菜单）
    batch.submitText("BACK", static_cast<float>(kDesignW) - 80.0f, 18.0f, 0.9f,
                     PackColor(0, 217, 255, 255));
}

/** 歌曲卡片网格：3 列布局 */
void SceneSongSelect::drawSongGrid(RenderBatch& batch, int screen_w, int screen_h) {
    const int cols = 3;
    const float padding = 24.0f;
    const float top_margin = 80.0f;      // Header 56 + 留白
    const float bottom_margin = 160.0f;  // BottomBar 上方留白

    float card_w = (static_cast<float>(screen_w) - padding * (cols + 1)) / cols;
    float card_h = card_w * 1.35f;       // 封面比例偏竖
    float content_h = static_cast<float>(screen_h) - top_margin - bottom_margin;

    // 裁剪有效绘制区域（简单的 Y 范围剔除）
    float min_visible_y = top_margin - card_h;
    float max_visible_y = static_cast<float>(screen_h) - bottom_margin + card_h;

    size_t idx = 0;
    for (const auto& song : songs_) {
        int col = static_cast<int>(idx % cols);
        int row = static_cast<int>(idx / cols);

        float cx = padding + col * (card_w + padding);
        float cy = top_margin + row * (card_h + padding) - scroll_y_;

        // 超出可视区域则跳过
        if (cy > max_visible_y || cy + card_h < min_visible_y) {
            ++idx;
            continue;
        }

        // ---------- 卡片背景 ----------
        batch.submitRect(cx, cy, card_w, card_h, PackColor(26, 26, 26, 255));

        // ---------- 封面占位（深灰色矩形 + 边框） ----------
        float cover_h = card_w * 0.9f;
        batch.submitRect(cx + 8.0f, cy + 8.0f, card_w - 16.0f, cover_h,
                         PackColor(45, 45, 45, 255));

        // ---------- 曲名 ----------
        float text_y = cy + cover_h + 20.0f;
        batch.submitText(song.title, cx + 16.0f, text_y, 0.9f,
                         PackColor(255, 255, 255, 255));

        // ---------- 难度标签（圆形 badge） ----------
        float tag_x = cx + 16.0f;
        float tag_y = text_y + 50.0f;
        float badge_size = 36.0f;
        float tag_gap = 8.0f;

        for (const auto& [diff_name, level] : song.difficulties) {
            uint32_t diff_color = DifficultyColor(diff_name);
            // 圆形 badge 用圆角矩形近似（半径为边长一半）
            batch.submitRoundedRect(tag_x, tag_y, badge_size, badge_size,
                                    badge_size * 0.5f, diff_color);
            // 难度等级数字（居中显示）
            std::string level_str = std::to_string(level);
            batch.submitText(level_str, tag_x + 6.0f, tag_y + 4.0f,
                             0.7f, PackColor(255, 255, 255, 255));
            tag_x += badge_size + tag_gap;
            if (tag_x + badge_size > cx + card_w - 8.0f) {
                break;
            }
        }

        ++idx;
    }
}

/** 底部筛选栏 */
void SceneSongSelect::drawBottomBar(RenderBatch& batch, int screen_w, int screen_h) {
    const float bar_h = 80.0f;
    float y = static_cast<float>(screen_h) - bar_h;

    batch.submitRect(0, y, static_cast<float>(screen_w), bar_h, PackColor(20, 20, 20, 255));
    float offset = static_cast<float>(g_stripe_time_ms_) * 0.05f;
    batch.submitStripedRect(0, y, static_cast<float>(screen_w), bar_h,
                            PackColor(20, 20, 20, 0),
                            PackColor(40, 40, 40, 64), 1, offset);

    batch.submitRect(0, y, static_cast<float>(screen_w), 2.0f, PackColor(40, 40, 40, 255));

    float btn_w = 140.0f;
    float btn_h = 44.0f;
    float start_x = 80.0f;
    float gap = 30.0f;

    batch.submitRoundedRect(start_x, y + 18.0f, btn_w, btn_h, 8.0f,
                            PackColor(60, 60, 60, 255));
    batch.submitText("ALL", start_x + 50.0f, y + 28.0f, 0.9f,
                     PackColor(255, 255, 255, 255));

    batch.submitRoundedRect(start_x + btn_w + gap, y + 18.0f, btn_w, btn_h, 8.0f,
                            PackColor(40, 40, 40, 255));
    batch.submitText("HARD+", start_x + btn_w + gap + 30.0f, y + 28.0f, 0.9f,
                     PackColor(200, 200, 200, 255));
}

// ============================================================
// 输入处理：滑动 + 点击
// ============================================================

void SceneSongSelect::handleInput(const std::vector<RawTouch>& touches,
                                  int64_t audio_now_ms) {
    (void)audio_now_ms;

    // 静态变量用于跨帧跟踪触摸状态（头文件无此成员时的替代方案）
    static std::unordered_map<int64_t, float> touch_start_y_norm;
    static std::unordered_map<int64_t, bool>  touch_is_drag;

    for (const auto& t : touches) {
        if (t.is_new && t.is_down) {
            // 检测返回按钮点击（右上角区域 Header 内）
            float px = t.x * kDesignW;
            float py = t.y * kDesignH;
            if (px >= static_cast<float>(kDesignW) - 120.0f && py <= 56.0f) {
                transition_request_.type = Transition::PUSH;
                transition_request_.target_scene_id = static_cast<int>(SceneID::MAIN_MENU);
                return;
            }

            // 新按下：记录起始位置
            touch_start_y_norm[t.finger_id] = t.y;
            touch_is_drag[t.finger_id] = false;
        }
        else if (t.is_down) {
            // 拖动中：计算位移并更新目标滚动值
            float dy_norm = t.y - touch_start_y_norm[t.finger_id];
            float dy_px = dy_norm * kDesignH;  // 归一化转像素

            // 超过阈值视为拖动而非点击
            if (std::fabs(dy_px) > 20.0f) {
                touch_is_drag[t.finger_id] = true;
                target_scroll_y_ -= dy_px;       // 手指上滑 -> 内容上移
                touch_start_y_norm[t.finger_id] = t.y;  // 重置基准
            }
        }
        else if (!t.is_down) {
            // 抬起：若不是拖动，则视为点击 -> 进入 gameplay
            auto it = touch_is_drag.find(t.finger_id);
            if (it != touch_is_drag.end() && !it->second) {
                transition_request_.type = Transition::PUSH;
                transition_request_.target_scene_id = static_cast<int>(SceneID::GAMEPLAY);
            }
            touch_start_y_norm.erase(t.finger_id);
            touch_is_drag.erase(t.finger_id);
        }
    }

    // 滚动边界限制
    const int cols = 3;
    const float padding = 24.0f;
    const float top_margin = 80.0f;
    const float bottom_margin = 120.0f;
    float card_w = (static_cast<float>(kDesignW) - padding * (cols + 1)) / cols;
    float card_h = card_w * 1.35f;
    int rows = static_cast<int>((songs_.size() + cols - 1) / cols);
    float total_content_h = top_margin + rows * (card_h + padding) + bottom_margin;
    float max_scroll = std::max(0.0f, total_content_h - kDesignH);

    if (target_scroll_y_ < 0.0f) target_scroll_y_ = 0.0f;
    if (target_scroll_y_ > max_scroll) target_scroll_y_ = max_scroll;
}
