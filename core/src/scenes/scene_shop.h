#pragma once
#include "scene_base.h"
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <chrono>

class Texture;
class RenderBatch;

// ============================================================
// 商店数据模型（接口定义阶段，数据来自 Mock 后端）
// ============================================================

enum class DifficultyColor : uint8_t {
    Blue = 0,    // 1-5
    Red = 1,     // 6-10
    Purple = 2,  // 11-14
    Gold = 3,    // 15+
    Gray = 4     // 不可用
};

enum class CurrencyType : uint8_t {
    Base = 0,    // 菱形银币
    Premium = 1  // 钻石
};

enum class ItemStatus : uint8_t {
    Free = 0,
    Owned = 1,
    Purchasable = 2,  // 可购买
    Locked = 3
};

struct DifficultyInfo {
    int32_t level = 0;
    DifficultyColor color = DifficultyColor::Blue;
};

struct ShopItem {
    int32_t id = 0;
    std::string title;
    std::string artist;
    std::string cover_path;
    std::vector<DifficultyInfo> difficulties;
    int32_t price = 0;
    CurrencyType currency_type = CurrencyType::Base;
    ItemStatus status = ItemStatus::Free;
    std::string category_tag;
};

struct PlayerState {
    int32_t level = 1;
    int64_t exp = 0;
    int32_t stamina_current = 0;
    int32_t stamina_max = 10;
    int64_t currency_base = 0;
    int64_t currency_premium = 0;
};

// ============================================================
// Store 筛选
// ============================================================

enum class SortType : uint8_t {
    Default = 0,
    DifficultyAsc = 1,
    DifficultyDesc = 2,
    DateNewest = 3,
    DateOldest = 4,
    TitleAZ = 5
};

enum class DateFilter : uint8_t {
    All = 0,
    ThisWeek = 1,
    ThisMonth = 2,
    ThisYear = 3
};

struct ShopFilterState {
    std::string search_keyword;
    std::string category_filter;
    SortType sort_type = SortType::Default;
    DateFilter date_filter = DateFilter::All;
};

// ============================================================
// 商店场景
// ============================================================

/**
 * 商店场景（横屏 1920x1080）
 *
 * 布局三层：Header(72dp) | Content(可纵向滚动, 3列卡片网格) | Footer(64dp)
 *
 * Footer 包含：
 *   ┌──────────────────────────────────────────────┐
 *   │  🔍 搜索...  [类别▼]  [全部]  [排序▼]  [日期▼] │
 *   └──────────────────────────────────────────────┘
 *   搜索框在左侧 | 四个筛选按钮靠右
 */
class SceneShop : public SceneBase {
public:
    void init() override;
    void enter() override;
    void exit() override;
    void update(int64_t audio_now_ms) override;
    void render(RenderBatch& batch, int64_t audio_now_ms) override;
    void handleInput(const std::vector<RawTouch>& touches, int64_t audio_now_ms) override;

private:
    // ====== 常量 ======
    static constexpr int kDesignW = 1920;
    static constexpr int kDesignH = 1080;
    static constexpr float kHeaderH = 72.0f;
    static constexpr float kFooterH = 64.0f;
    static constexpr float kCardW = 560.0f;
    static constexpr float kCardH = 280.0f;
    static constexpr float kCardGapX = 24.0f;
    static constexpr float kCardGapY = 20.0f;
    static constexpr float kCoverW = 200.0f;
    static constexpr float kCoverH = 240.0f;
    static constexpr float kBadgeSize = 24.0f;
    static constexpr int kColumns = 3;
    static constexpr float kContentPadX = 40.0f;
    static constexpr float kContentPadY = 24.0f;
    static constexpr float kScrollFriction = 0.88f;
    static constexpr float kScrollMinVelocity = 10.0f;
    static constexpr float kScrollMaxVelocity = 3000.0f;
    static constexpr float kRubberBandMax = 80.0f;

    // ====== 数据 ======
    std::vector<ShopItem> items_;
    PlayerState player_;
    ShopFilterState filter_;
    int32_t current_page_ = 0;
    bool has_more_ = true;
    bool is_loading_ = false;

    std::vector<std::unique_ptr<Texture>> cover_cache_;

    // ====== 滚动状态 ======
    float scroll_y_ = 0.0f;
    float velocity_ = 0.0f;
    bool is_dragging_ = false;
    float drag_start_y_ = 0.0f;
    float drag_start_scroll_ = 0.0f;

    // ====== Footer 下拉菜单状态 ======
    bool show_dropdown_ = false;
    int dropdown_type_ = 0; // 0=none, 1=category, 2=sort, 3=date

    // ====== 内部方法 ======
    void loadMockSongs();
    void drawHeader(RenderBatch& batch);
    void drawContent(RenderBatch& batch, int screen_w, int screen_h);
    void drawCard(RenderBatch& batch, const ShopItem& item, float x, float y);
    void drawFooter(RenderBatch& batch, int screen_w, int screen_h);
    void drawSearchBox(RenderBatch& batch, float x, float y, float w, float h);
    void drawFilterButtons(RenderBatch& batch, float x, float y);
    void drawDropdown(RenderBatch& batch);

    void doScroll(float delta_y);
    float getContentHeight() const;
    float designScaleX(int screen_w) const { return static_cast<float>(screen_w) / kDesignW; }
    float designScaleY(int screen_h) const { return static_cast<float>(screen_h) / kDesignH; }

    static inline uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    static uint32_t diffColor(DifficultyColor dc, uint8_t alpha = 255);
};
