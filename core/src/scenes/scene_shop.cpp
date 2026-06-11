/**
 * 商店场景实现 —— 谱面商店（横屏 1920x1080）
 *
 * 布局：Header(72dp) | Content(弹性填充, 3列卡片, 纵向滚动) | Footer(64dp)
 *
 * Footer:
 *   ┌──────────────────────────────────────────────────────┐
 *   │  🔍 搜索...                   [类别▼] [全部] [排序▼] [日期▼]│
 *   └──────────────────────────────────────────────────────┘
 *
 * 本阶段仅为接口实现，数据来自 MockShopBackend，后续接入文件服务器。
 */

#include "scenes/scene_shop.h"
#include "engine/render_batch.h"
#include "engine/input_manager.h"
#include "utils/logger.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include "../gameplay/gameplay_ui_config.hpp"

// ============================================================
// 工具函数
// ============================================================


static uint32_t diffColor(DifficultyColor dc, uint8_t alpha) {
    switch (dc) {
        case DifficultyColor::Blue:   return PackColor(59, 130, 246, alpha);
        case DifficultyColor::Red:    return PackColor(239, 68, 68, alpha);
        case DifficultyColor::Purple: return PackColor(139, 92, 246, alpha);
        case DifficultyColor::Gold:   return PackColor(245, 158, 11, alpha);
        case DifficultyColor::Gray:   return PackColor(107, 114, 128, alpha);
    }
    return PackColor(107, 114, 128, alpha);
}

static DifficultyColor levelToColor(int32_t level) {
    if (level <= 5)  return DifficultyColor::Blue;
    if (level <= 10) return DifficultyColor::Red;
    if (level <= 14) return DifficultyColor::Purple;
    return DifficultyColor::Gold;
}

uint32_t SceneShop::PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ::PackColor(r, g, b, a);
}

uint32_t SceneShop::diffColor(DifficultyColor dc, uint8_t alpha) {
    return ::diffColor(dc, alpha);
}

// ============================================================
// 生命周期
// ============================================================

void SceneShop::init() {
    loadMockSongs();
}

void SceneShop::enter() {
    scroll_y_ = 0.0f;
    velocity_ = 0.0f;
}

void SceneShop::exit() {
    cover_cache_.clear();
}

void SceneShop::update(int64_t audio_now_ms) {
    stripe_time_ms_ = audio_now_ms;  // 驱动斜纹滚动
    (void)audio_now_ms;

    // 惯性滚动衰减
    if (!is_dragging_ && std::abs(velocity_) > kScrollMinVelocity) {
        scroll_y_ += velocity_ * 0.016f; // ~60fps delta
        velocity_ *= kScrollFriction;

        if (std::abs(velocity_) < kScrollMinVelocity) {
            velocity_ = 0.0f;
        }

        // Rubber band 回弹
        float max_scroll = std::max(0.0f, getContentHeight() - static_cast<float>(kDesignH) + kHeaderH + kFooterH);
        if (scroll_y_ < -kRubberBandMax) {
            scroll_y_ = -kRubberBandMax;
            velocity_ = 0.0f;
        } else if (scroll_y_ < 0.0f) {
            scroll_y_ *= 0.8f; // 弹性回弹
        } else if (scroll_y_ > max_scroll + kRubberBandMax) {
            scroll_y_ = max_scroll + kRubberBandMax;
            velocity_ = 0.0f;
        } else if (scroll_y_ > max_scroll) {
            scroll_y_ = max_scroll + (scroll_y_ - max_scroll) * 0.8f;
        }
    }
}

// ============================================================
// Mock 数据生成
// ============================================================

void SceneShop::loadMockSongs() {
    items_.clear();
    cover_cache_.clear();

    // Mock 玩家状态
    player_ = {15, 12500, 7, 10, 3400, 120};

    struct MockSong {
        const char* title;
        const char* artist;
        int32_t price;
        CurrencyType currency;
        const char* category;
        std::vector<int32_t> levels;
    };

    MockSong mock_songs[] = {
        {"Rosenkreuz", "TAG", 0, CurrencyType::Base, "POP", {3, 7, 10, 15}},
        {"Vampir", "TAG feat. JUN", 500, CurrencyType::Base, "POP", {4, 8, 12}},
        {"Grievous Lady", "Team Grimoire", 800, CurrencyType::Base, "HARD", {6, 11, 14}},
        {"Conflict", "sirok", 300, CurrencyType::Base, "POP", {2, 5, 9}},
        {"Axium Crisis", "ak+q", 1200, CurrencyType::Base, "HARD", {7, 12, 15, 16}},
        {"Cyanine", "Jayn", 0, CurrencyType::Base, "FREE", {1, 4, 8}},
        {"Red and Blue", "Silentroom", 600, CurrencyType::Premium, "POP", {5, 9, 13}},
        {"Lunar Sunrise", "Ryu*", 400, CurrencyType::Base, "POP", {3, 7, 11}},
        {"Crosswind", "Street", 900, CurrencyType::Base, "HARD", {8, 12, 15}},
        {"Halcyon", "xi", 1500, CurrencyType::Premium, "HARD", {9, 13, 16, 17}},
        {"Snow White", "Puru", 0, CurrencyType::Base, "FREE", {1, 3, 6}},
        {"Glow", "Nhato", 700, CurrencyType::Base, "EVENT", {4, 8, 12}},
        {"Future Candy", "Y&Co.", 550, CurrencyType::Base, "POP", {2, 6, 10}},
        {"Brain Power", "NOMA", 1000, CurrencyType::Premium, "POP", {7, 11, 14}},
        {"Dement", "Lime", 350, CurrencyType::Base, "HARD", {5, 10, 13}},
        {"Stasis", "Aether", 850, CurrencyType::Base, "EVENT", {6, 11, 15}},
        {"Nhelv", "Silentroom", 2000, CurrencyType::Premium, "HARD", {10, 14, 17}},
        {"Altale", "Morimori Atsushi", 0, CurrencyType::Base, "FREE", {1, 5, 9}},
        {"Sakura", "Cranky", 450, CurrencyType::Base, "POP", {4, 8, 12}},
        {"Energy", "Nhato", 1200, CurrencyType::Premium, "EVENT", {8, 13, 16}},
    };

    int32_t id = 1;
    for (const auto& ms : mock_songs) {
        ShopItem item;
        item.id = id++;
        item.title = ms.title;
        item.artist = ms.artist;
        item.price = ms.price;
        item.currency_type = ms.currency;
        item.category_tag = ms.category;
        item.status = (item.price == 0) ? ItemStatus::Free : ItemStatus::Purchasable;
        for (int32_t lv : ms.levels) {
            item.difficulties.push_back({lv, levelToColor(lv)});
        }
        items_.push_back(std::move(item));
    }
    has_more_ = false; // Mock 一次性加载完毕
    current_page_ = 1;
}

// ============================================================
// 渲染
// ============================================================

void SceneShop::render(RenderBatch& batch, int64_t audio_now_ms) {
    (void)audio_now_ms;

    batch.submitRect(0.0f, 0.0f, static_cast<float>(kDesignW), static_cast<float>(kDesignH),
                     PackColor(15, 15, 15, 19)); // #0F0F13

    drawHeader(batch);
    drawContent(batch, kDesignW, kDesignH);
    drawFooter(batch, kDesignW, kDesignH);

    if (show_dropdown_) {
        drawDropdown(batch);
    }
}

// ============================================================
// Header 渲染
// ============================================================

void SceneShop::drawHeader(RenderBatch& batch) {
    float hw = static_cast<float>(kDesignW);
    float hh = kHeaderH;

    // Header 背景
    batch.submitRect(0.0f, 0.0f, hw, hh, PackColor(26, 26, 31, 255));

    // 头像占位（圆形，用圆角矩形近似）
    batch.submitRoundedRect(12.0f, 10.0f, 52.0f, 52.0f, 26.0f,
                            PackColor(59, 130, 246, 255));

    // 用户名
    batch.submitText("GUEST", 76.0f, 18.0f, 0.9f,
                     PackColor(255, 255, 255, 255));

    // 等级
    std::string level_str = "Lv." + std::to_string(player_.level);
    batch.submitText(level_str, 76.0f, 42.0f, 0.6f,
                     PackColor(156, 163, 175, 255), true);

    // 体力条
    float stamina_x = 300.0f;
    float stamina_w = 120.0f;
    float stamina_h = 8.0f;
    batch.submitRoundedRect(stamina_x, 32.0f, stamina_w, stamina_h, 4.0f,
                            PackColor(55, 55, 65, 255));
    float fill_ratio = static_cast<float>(player_.stamina_current) / player_.stamina_max;
    if (fill_ratio > 0.0f) {
        batch.submitRoundedRect(stamina_x, 32.0f, stamina_w * fill_ratio, stamina_h, 4.0f,
                                PackColor(74, 222, 128, 255));
    }
    std::string stamina_str = std::to_string(player_.stamina_current) + "/" + std::to_string(player_.stamina_max);
    batch.submitText(stamina_str, stamina_x, 14.0f, 0.55f,
                     PackColor(226, 232, 240, 255));

    // 货币（菱形币）
    std::string coin_str = "♢ " + std::to_string(player_.currency_base);
    batch.submitText(coin_str, stamina_x + stamina_w + 40.0f, 16.0f, 0.8f,
                     PackColor(226, 232, 240, 255), true);

    // 货币（钻石）
    std::string diamond_str = "◇ " + std::to_string(player_.currency_premium);
    batch.submitText(diamond_str, stamina_x + stamina_w + 40.0f, 38.0f, 0.8f,
                     PackColor(56, 189, 248, 255), true);

    // 返回按钮（右上）
    batch.submitRoundedRect(hw - 100.0f, 14.0f, 80.0f, 44.0f, 22.0f,
                            PackColor(55, 55, 65, 255));
    batch.submitText("BACK", hw - 86.0f, 26.0f, 0.7f,
                     PackColor(255, 255, 255, 255));

    // 标题
    batch.submitText("SHOP", hw * 0.5f - 45.0f, 22.0f, 1.0f,
                     PackColor(255, 255, 255, 255));
}

// ============================================================
// Content 渲染（3列卡片网格 + 纵向滚动）
// ============================================================

void SceneShop::drawContent(RenderBatch& batch, int screen_w, int screen_h) {
    if (items_.empty()) {
        // 空状态
        batch.submitText("No songs found", static_cast<float>(screen_w) * 0.5f - 80.0f,
                         static_cast<float>(screen_h) * 0.5f, 1.2f,
                         PackColor(156, 163, 175, 255));
        return;
    }

    float content_top = kHeaderH;
    float content_bot = static_cast<float>(screen_h) - kFooterH;

    // 计算卡片布局
    float total_card_w = kColumns * kCardW + (kColumns - 1) * kCardGapX;
    float grid_start_x = (static_cast<float>(screen_w) - total_card_w) * 0.5f;

    int total_rows = static_cast<int>((items_.size() + kColumns - 1) / kColumns);

    for (int row = 0; row < total_rows; ++row) {
        float card_y = content_top + kContentPadY + row * (kCardH + kCardGapY) - scroll_y_;

        // 视口裁剪：只渲染可见行
        if (card_y + kCardH < content_top || card_y > content_bot) {
            continue;
        }

        for (int col = 0; col < kColumns; ++col) {
            int idx = row * kColumns + col;
            if (idx >= static_cast<int>(items_.size())) break;

            float card_x = grid_start_x + col * (kCardW + kCardGapX);
            drawCard(batch, items_[idx], card_x, card_y);
        }
    }
}

void SceneShop::drawCard(RenderBatch& batch, const ShopItem& item, float x, float y) {
    // 卡片背景（圆角矩形）
    batch.submitRoundedRect(x, y, kCardW, kCardH, 12.0f, PackColor(36, 36, 43, 255));

    // 封面缩略图区域（左侧，圆角）
    float cover_x = x + 12.0f;
    float cover_y = y + (kCardH - kCoverH) * 0.5f;
    batch.submitRoundedRect(cover_x, cover_y, kCoverW, kCoverH, 8.0f,
                            PackColor(26, 26, 31, 255));
    // 封面占位图：用渐变色近似
    batch.submitRect(cover_x + 4.0f, cover_y + 4.0f, kCoverW - 8.0f, kCoverH - 8.0f,
                     PackColor(40 + (item.id * 20) % 60, 30 + (item.id * 40) % 60,
                               50 + (item.id * 30) % 60, 255));

    // 曲名
    float text_x = cover_x + kCoverW + 16.0f;
    float text_y = y + 20.0f;
    batch.submitText(item.title, text_x, text_y, 1.0f,
                     PackColor(255, 255, 255, 255));

    // 艺术家
    batch.submitText(item.artist, text_x, text_y + 30.0f, 0.7f,
                     PackColor(156, 163, 175, 255));

    // 分类标签
    batch.submitRoundedRect(text_x, text_y + 60.0f, 80.0f, 22.0f, 4.0f,
                            PackColor(55, 55, 65, 200));
    batch.submitText(item.category_tag, text_x + 8.0f, text_y + 64.0f, 0.5f,
                     PackColor(156, 163, 175, 255));

    // 难度 Badge 列表（横向排列）
    float badge_start_x = text_x;
    float badge_y = y + kCardH - 40.0f;
    for (size_t i = 0; i < item.difficulties.size() && i < 4; ++i) {
        const auto& d = item.difficulties[i];
        float bw = 40.0f;
        float bh = kBadgeSize;
        float bx = badge_start_x + i * (bw + 6.0f);
        batch.submitRoundedRect(bx, badge_y, bw, bh, 4.0f,
                                SceneShop::diffColor(d.color, 200));
        batch.submitText(std::to_string(d.level), bx + 10.0f, badge_y + 4.0f, 0.5f,
                         PackColor(255, 255, 255, 255));
    }

    // 价格与购买按钮（右下角）
    float price_x = x + kCardW - 100.0f;
    float price_y = y + kCardH - 36.0f;

    if (item.status == ItemStatus::Owned || item.status == ItemStatus::Free) {
        // 已拥有 / 免费
        batch.submitRoundedRect(price_x, price_y, 80.0f, 26.0f, 13.0f,
                                PackColor(74, 222, 128, 180));
        batch.submitText("OWNED", price_x + 10.0f, price_y + 5.0f, 0.5f,
                         PackColor(255, 255, 255, 255));
    } else if (item.status == ItemStatus::Purchasable) {
        // 可购买
        uint32_t price_color = (item.currency_type == CurrencyType::Premium)
            ? PackColor(56, 189, 248, 255)
            : PackColor(226, 232, 240, 255);
        batch.submitRoundedRect(price_x, price_y, 80.0f, 26.0f, 13.0f,
                                PackColor(59, 130, 246, 180));
        std::string price_str = std::to_string(item.price);
        batch.submitText(price_str, price_x + 10.0f, price_y + 5.0f, 0.5f, price_color, true);
    } else {
        // 锁定
        batch.submitRoundedRect(price_x, price_y, 80.0f, 26.0f, 13.0f,
                                PackColor(55, 55, 65, 200));
        batch.submitText("LOCKED", price_x + 8.0f, price_y + 5.0f, 0.5f,
                         PackColor(107, 114, 128, 255));
    }

    // 高亮边框
    batch.submitRect(x + 2.0f, y + kCardH - 2.0f, kCardW - 4.0f, 2.0f,
                     PackColor(59, 130, 246, 60));
}

// ============================================================
// Footer 渲染（搜索框 + 筛选按钮）
// ============================================================

void SceneShop::drawFooter(RenderBatch& batch, int screen_w, int screen_h) {
    float fy = static_cast<float>(screen_h) - kFooterH;

    // Footer 背景
    batch.submitRect(0.0f, fy, static_cast<float>(screen_w), kFooterH,
                     PackColor(15, 15, 15, 240));

    // 搜索框（左侧）
    float search_w = 340.0f;
    float search_h = 36.0f;
    float search_x = kContentPadX;
    float search_y = fy + (kFooterH - search_h) * 0.5f;
    drawSearchBox(batch, search_x, search_y, search_w, search_h);

    // 筛选按钮（右侧）
    float filter_x = static_cast<float>(screen_w) - 480.0f;
    drawFilterButtons(batch, filter_x, fy);
}

void SceneShop::drawSearchBox(RenderBatch& batch, float x, float y, float w, float h) {
    // 搜索框背景
    batch.submitRoundedRect(x, y, w, h, 18.0f, PackColor(36, 36, 43, 255));

    // 放大镜图标（圆+直线）
    float icon_size = 12.0f;
    float icon_x = x + 14.0f;
    float icon_y = y + (h - icon_size) * 0.5f;
    // 用两个小矩形模拟放大镜
    batch.submitRect(icon_x, icon_y, icon_size, icon_size, PackColor(156, 163, 175, 255));
    batch.submitRect(icon_x + icon_size - 2.0f, icon_y + icon_size - 2.0f,
                     4.0f, 4.0f, PackColor(156, 163, 175, 255));

    // 占位文字
    std::string display_text = filter_.search_keyword.empty()
        ? "Search songs..." : filter_.search_keyword;
    batch.submitText(display_text, x + 34.0f, y + 9.0f, 0.65f,
                     filter_.search_keyword.empty()
                        ? PackColor(107, 114, 128, 255)
                        : PackColor(255, 255, 255, 255));

    // 如果有关键字，显示清除按钮
    if (!filter_.search_keyword.empty()) {
        batch.submitRoundedRect(x + w - 28.0f, y + 8.0f, 20.0f, 20.0f, 10.0f,
                                PackColor(107, 114, 128, 200));
        batch.submitText("x", x + w - 24.0f, y + 10.0f, 0.55f,
                         PackColor(255, 255, 255, 255));
    }
}

void SceneShop::drawFilterButtons(RenderBatch& batch, float x, float y) {
    struct FilterBtn {
        const char* label;
        int type; // dropdown_type_
    };
    FilterBtn btns[4] = {
        {"CATEGORY", 1},
        {"SORT", 2},
        {"DATE", 3},
    };

    float btn_w = 110.0f;
    float btn_h = 34.0f;
    float gap = 10.0f;
    float btn_y = y + (kFooterH - btn_h) * 0.5f;

    for (int i = 0; i < 3; ++i) {
        float bx = x + i * (btn_w + gap);

        // 筛选按钮背景（圆角矩形）
        uint32_t bg = PackColor(36, 36, 43, 255);
        if (show_dropdown_ && dropdown_type_ == btns[i].type) {
            bg = PackColor(55, 55, 65, 255); // 选中态高亮
        }
        batch.submitRoundedRect(bx, btn_y, btn_w, btn_h, 6.0f, bg);

        // 按钮文字
        batch.submitText(btns[i].label, bx + 8.0f, btn_y + 8.0f, 0.55f,
                         PackColor(226, 232, 240, 255));

        // 下拉箭头 ▼
        batch.submitText("▼", bx + btn_w - 18.0f, btn_y + 8.0f, 0.45f,
                         PackColor(156, 163, 175, 255));
    }
}

// ============================================================
// 下拉菜单渲染
// ============================================================

void SceneShop::drawDropdown(RenderBatch& batch) {
    // 半透明遮罩
    batch.submitRect(0.0f, 0.0f, static_cast<float>(kDesignW), static_cast<float>(kDesignH),
                     PackColor(0, 0, 0, 100));

    // 菜单面板（在底部 Footer 上方弹出）
    float panel_w = 240.0f;
    float panel_h = 200.0f;
    float panel_x = static_cast<float>(kDesignW) - 460.0f;
    float panel_y = static_cast<float>(kDesignH) - kFooterH - panel_h - 8.0f;

    batch.submitRoundedRect(panel_x, panel_y, panel_w, panel_h, 12.0f,
                            PackColor(30, 30, 38, 255));

    // 菜单项（Mock 数据）
    const char* items[5] = {"All", "POP", "HARD", "EVENT", "FREE"};
    float item_h = 36.0f;
    for (int i = 0; i < 5; ++i) {
        float iy = panel_y + 8.0f + i * item_h;
        // 悬停高亮（这里用隔行变色模拟）
        if (i % 2 == 0) {
            batch.submitRect(panel_x + 4.0f, iy, panel_w - 8.0f, item_h - 2.0f,
                             PackColor(36, 36, 43, 100));
        }
        batch.submitText(items[i], panel_x + 20.0f, iy + 9.0f, 0.7f,
                         PackColor(255, 255, 255, 255));
    }
}

// ============================================================
// 滚动系统
// ============================================================

float SceneShop::getContentHeight() const {
    int total_rows = static_cast<int>((items_.size() + kColumns - 1) / kColumns);
    return kContentPadY + total_rows * (kCardH + kCardGapY) + kContentPadY;
}

void SceneShop::doScroll(float delta_y) {
    scroll_y_ += delta_y;
    float max_scroll = std::max(0.0f, getContentHeight() - (static_cast<float>(kDesignH) - kHeaderH - kFooterH));
    scroll_y_ = std::max(-kRubberBandMax, std::min(scroll_y_, max_scroll + kRubberBandMax));
}

// ============================================================
// 输入处理
// ============================================================

void SceneShop::handleInput(const std::vector<RawTouch>& touches,
                            int64_t audio_now_ms) {
    (void)audio_now_ms;

    for (const auto& t : touches) {
        float px = t.x * kDesignW;
        float py = t.y * kDesignH;

        if (t.is_new && t.is_down) {
            // 单击检测

            // 返回按钮（右上角）
            if (HitTest(px, py, static_cast<float>(kDesignW) - 100.0f, 0.0f,
                        100.0f, kHeaderH)) {
                transition_request_.type = Transition::POP;
                return;
            }

            // Footer 区域的搜索框点击
            float fy = static_cast<float>(kDesignH) - kFooterH;
            if (py >= fy && py <= fy + kFooterH) {
                // 搜索框区域
                if (px >= kContentPadX && px <= kContentPadX + 340.0f) {
                    Logger::info("Search box clicked - keyboard input TBD");
                    return;
                }

                // 筛选按钮区域
                float btn_x = static_cast<float>(kDesignW) - 480.0f;
                float btn_w = 110.0f;
                float gap = 10.0f;
                for (int i = 0; i < 3; ++i) {
                    float bx = btn_x + i * (btn_w + gap);
                    if (HitTest(px, py, bx, fy, btn_w, kFooterH)) {
                        if (show_dropdown_ && dropdown_type_ == i + 1) {
                            show_dropdown_ = false;
                            dropdown_type_ = 0;
                        } else {
                            show_dropdown_ = true;
                            dropdown_type_ = i + 1;
                        }
                        return;
                    }
                }

                // 点 Footer 其他地方关闭下拉
                show_dropdown_ = false;
                dropdown_type_ = 0;
                return;
            }

            // 点击下拉菜单外部关闭
            if (show_dropdown_) {
                float panel_w = 240.0f;
                float panel_h = 200.0f;
                float panel_x = static_cast<float>(kDesignW) - 460.0f;
                float panel_y = static_cast<float>(kDesignH) - kFooterH - panel_h - 8.0f;
                if (px < panel_x || px > panel_x + panel_w ||
                    py < panel_y || py > panel_y + panel_h) {
                    show_dropdown_ = false;
                    dropdown_type_ = 0;
                } else {
                    // 点击菜单项
                    int item_idx = static_cast<int>((py - panel_y - 8.0f) / 36.0f);
                    if (item_idx >= 0 && item_idx < 5) {
                        const char* categories[5] = {"", "POP", "HARD", "EVENT", "FREE"};
                        filter_.category_filter = categories[item_idx];
                        show_dropdown_ = false;
                        dropdown_type_ = 0;
                        Logger::info("Filter changed to: {}", filter_.category_filter);
                    }
                    return;
                }
            }

            // Content 区域：检测卡片点击
            float content_top = kHeaderH;
            float total_card_w = kColumns * kCardW + (kColumns - 1) * kCardGapX;
            float grid_start_x = (static_cast<float>(kDesignW) - total_card_w) * 0.5f;

            int total_rows = static_cast<int>((items_.size() + kColumns - 1) / kColumns);
            for (int row = 0; row < total_rows; ++row) {
                float card_y = content_top + kContentPadY + row * (kCardH + kCardGapY) - scroll_y_;
                if (py < card_y || py > card_y + kCardH) continue;

                for (int col = 0; col < kColumns; ++col) {
                    int idx = row * kColumns + col;
                    if (idx >= static_cast<int>(items_.size())) break;

                    float card_x = grid_start_x + col * (kCardW + kCardGapX);
                    if (px >= card_x && px <= card_x + kCardW) {
                        Logger::info("Card clicked: {} - {}", items_[idx].title, items_[idx].artist);
                        // TODO: 跳转谱面详情或直接进入 Gameplay
                        return;
                    }
                }
            }

            // 开始拖动
            if (py > content_top && py < static_cast<float>(kDesignH) - kFooterH) {
                is_dragging_ = true;
                drag_start_y_ = py;
                drag_start_scroll_ = scroll_y_;
                velocity_ = 0.0f;
            }

        } else if (t.is_down && is_dragging_) {
            // 拖动中
            float dy = py - drag_start_y_;
            float max_scroll = std::max(0.0f, getContentHeight() - (static_cast<float>(kDesignH) - kHeaderH - kFooterH));
            float new_scroll = drag_start_scroll_ - dy;
            scroll_y_ = std::max(-kRubberBandMax, std::min(new_scroll, max_scroll + kRubberBandMax));

        } else if (!t.is_down && is_dragging_) {
            // 拖动结束 → 惯性
            float dy = py - drag_start_y_;
            float duration_s = 0.016f; // 近似
            if (duration_s > 0.0f) {
                float vel = -dy / duration_s;
                vel = std::max(-kScrollMaxVelocity, std::min(vel, kScrollMaxVelocity));
                velocity_ = vel;
            }
            is_dragging_ = false;
        }
    }

    // 触摸结束但手指不在 touches 中时停止拖动
    if (!is_dragging_) {
        // 惯性在 update 中处理
    }
}
