// E1-16 螢幕邊緣熱區 — 單元測試（gtest）
//
// 驗證 EdgeHotZoneRegistry 於相位 1（Mac / null 期）的具名邊/角熱區判定：
//   - 四邊熱區觸發（Left / Right / Top / Bottom）
//   - 四角熱區觸發（TopLeft / TopRight / BottomLeft / BottomRight）
//   - 厚度比例換算（不同 thickness_ratio 對應不同熱區範圍）
//   - 邊界情形（熱區邊界含邊界 / 剛好落在熱區外一點不命中）
//   - 多熱區（同邊重複註冊，後者為準；多個不同熱區並存）
//   - 角落優先於邊（同時落入某角與某邊時回角落）
//   - 無效邊 / 無效厚度比例報錯不靜默（register_zone 回非 Ok 狀態，且不註冊）
//   - 不同螢幕尺寸（同一 ratio 在不同 ScreenExtent 下換算出不同的絕對熱區範圍）
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實視窗系統 / 事件迴圈。
#include "edge_hot_zone.hpp"

#include <gtest/gtest.h>

#include <limits>

using ds::kernel::EdgeHotZone;
using ds::kernel::EdgeHotZoneRegistry;
using ds::kernel::HotZoneAction;
using ds::kernel::HotZoneStatus;
using ds::kernel::is_named_zone;
using ds::kernel::LocalPoint;
using ds::kernel::ScreenExtent;
using ds::kernel::TriggeredHotZone;

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();

// -----------------------------------------------------------------------------
// 四邊熱區觸發
// -----------------------------------------------------------------------------

TEST(EdgeZones, LeftTriggers) {
    EdgeHotZoneRegistry reg;
    EXPECT_EQ(reg.register_zone(EdgeHotZone::Left, 0.1f, "open_sidebar"), HotZoneStatus::Ok);

    ScreenExtent screen{1000.0f, 800.0f};
    auto hit = reg.test(LocalPoint{5.0f, 400.0f}, screen);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->zone, EdgeHotZone::Left);
    EXPECT_EQ(hit->action, "open_sidebar");

    // 遠離左邊緣不觸發。
    EXPECT_FALSE(reg.test(LocalPoint{500.0f, 400.0f}, screen).has_value());
}

TEST(EdgeZones, RightTriggers) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::Right, 0.1f, "show_dock");
    ScreenExtent screen{1000.0f, 800.0f};

    auto hit = reg.test(LocalPoint{995.0f, 400.0f}, screen);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->zone, EdgeHotZone::Right);

    EXPECT_FALSE(reg.test(LocalPoint{500.0f, 400.0f}, screen).has_value());
}

TEST(EdgeZones, TopTriggers) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::Top, 0.05f, "reveal_menu_bar");
    ScreenExtent screen{1000.0f, 800.0f};

    auto hit = reg.test(LocalPoint{500.0f, 2.0f}, screen);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->zone, EdgeHotZone::Top);

    EXPECT_FALSE(reg.test(LocalPoint{500.0f, 400.0f}, screen).has_value());
}

TEST(EdgeZones, BottomTriggers) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::Bottom, 0.05f, "show_taskbar");
    ScreenExtent screen{1000.0f, 800.0f};

    auto hit = reg.test(LocalPoint{500.0f, 798.0f}, screen);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->zone, EdgeHotZone::Bottom);

    EXPECT_FALSE(reg.test(LocalPoint{500.0f, 400.0f}, screen).has_value());
}

// -----------------------------------------------------------------------------
// 四角熱區觸發
// -----------------------------------------------------------------------------

TEST(CornerZones, AllFourCornersTrigger) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::TopLeft, 0.1f, "tl");
    reg.register_zone(EdgeHotZone::TopRight, 0.1f, "tr");
    reg.register_zone(EdgeHotZone::BottomLeft, 0.1f, "bl");
    reg.register_zone(EdgeHotZone::BottomRight, 0.1f, "br");
    ScreenExtent screen{1000.0f, 800.0f};

    auto tl = reg.test(LocalPoint{2.0f, 2.0f}, screen);
    ASSERT_TRUE(tl.has_value());
    EXPECT_EQ(tl->zone, EdgeHotZone::TopLeft);
    EXPECT_EQ(tl->action, "tl");

    auto tr = reg.test(LocalPoint{998.0f, 2.0f}, screen);
    ASSERT_TRUE(tr.has_value());
    EXPECT_EQ(tr->zone, EdgeHotZone::TopRight);

    auto bl = reg.test(LocalPoint{2.0f, 798.0f}, screen);
    ASSERT_TRUE(bl.has_value());
    EXPECT_EQ(bl->zone, EdgeHotZone::BottomLeft);

    auto br = reg.test(LocalPoint{998.0f, 798.0f}, screen);
    ASSERT_TRUE(br.has_value());
    EXPECT_EQ(br->zone, EdgeHotZone::BottomRight);

    // 螢幕中央不落入任何角落。
    EXPECT_FALSE(reg.test(LocalPoint{500.0f, 400.0f}, screen).has_value());
}

// -----------------------------------------------------------------------------
// 厚度比例
// -----------------------------------------------------------------------------

TEST(Thickness, LargerRatioCoversMorePoint) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::Left, 0.5f, "wide");
    ScreenExtent screen{1000.0f, 800.0f};

    // ratio 0.5 → 熱區延伸至 x=500，故 x=400 應命中。
    EXPECT_TRUE(reg.test(LocalPoint{400.0f, 400.0f}, screen).has_value());
    // x=600 超出 0.5 比例熱區。
    EXPECT_FALSE(reg.test(LocalPoint{600.0f, 400.0f}, screen).has_value());
}

TEST(Thickness, FullRatioCoversEntireEdge) {
    EdgeHotZoneRegistry reg;
    // ratio = 1.0 為合法上限：整個螢幕寬度都算 Top 熱區。
    reg.register_zone(EdgeHotZone::Top, 1.0f, "full_top");
    ScreenExtent screen{1000.0f, 800.0f};

    EXPECT_TRUE(reg.test(LocalPoint{500.0f, 799.0f}, screen).has_value());
}

// -----------------------------------------------------------------------------
// 邊界情形
// -----------------------------------------------------------------------------

TEST(Boundary, InclusiveAtRatioEdge) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::Left, 0.1f, "edge_action");
    ScreenExtent screen{1000.0f, 800.0f};

    // 熱區恰好延伸至 x=100（含邊界，應命中）。
    EXPECT_TRUE(reg.test(LocalPoint{100.0f, 400.0f}, screen).has_value());
    // x=100.5 已超出熱區（不命中）。
    EXPECT_FALSE(reg.test(LocalPoint{100.5f, 400.0f}, screen).has_value());
    // y 軸邊界（0 與 height）亦含邊界。
    EXPECT_TRUE(reg.test(LocalPoint{0.0f, 0.0f}, screen).has_value());
    EXPECT_TRUE(reg.test(LocalPoint{0.0f, 800.0f}, screen).has_value());
}

// -----------------------------------------------------------------------------
// 多熱區
// -----------------------------------------------------------------------------

TEST(MultiZone, LaterRegistrationWinsOnOverlap) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::Left, 0.1f, "first");
    reg.register_zone(EdgeHotZone::Left, 0.1f, "second");  // 同邊重複註冊
    ScreenExtent screen{1000.0f, 800.0f};

    auto hit = reg.test(LocalPoint{5.0f, 400.0f}, screen);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->action, "second");  // 後者為準
}

TEST(MultiZone, DistinctZonesCoexist) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::Left, 0.1f, "left_action");
    reg.register_zone(EdgeHotZone::Right, 0.1f, "right_action");
    reg.register_zone(EdgeHotZone::Top, 0.1f, "top_action");
    EXPECT_EQ(reg.size(), static_cast<std::size_t>(3));
    ScreenExtent screen{1000.0f, 800.0f};

    EXPECT_EQ(reg.test(LocalPoint{5.0f, 400.0f}, screen)->zone, EdgeHotZone::Left);
    EXPECT_EQ(reg.test(LocalPoint{995.0f, 400.0f}, screen)->zone, EdgeHotZone::Right);
    EXPECT_EQ(reg.test(LocalPoint{500.0f, 5.0f}, screen)->zone, EdgeHotZone::Top);
}

// -----------------------------------------------------------------------------
// 角落優先於邊
// -----------------------------------------------------------------------------

TEST(Priority, CornerBeatsAdjacentEdges) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::Left, 0.2f, "left_edge");
    reg.register_zone(EdgeHotZone::Top, 0.2f, "top_edge");
    reg.register_zone(EdgeHotZone::TopLeft, 0.2f, "top_left_corner");
    ScreenExtent screen{1000.0f, 800.0f};

    // (10, 10) 同時落入 Left、Top、TopLeft 三個熱區範圍——角落優先。
    auto hit = reg.test(LocalPoint{10.0f, 10.0f}, screen);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->zone, EdgeHotZone::TopLeft);
    EXPECT_EQ(hit->action, "top_left_corner");
}

TEST(Priority, FallsBackToEdgeWhenNoCornerRegistered) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::Left, 0.2f, "left_edge");
    reg.register_zone(EdgeHotZone::Top, 0.2f, "top_edge");
    // 未註冊 TopLeft。
    ScreenExtent screen{1000.0f, 800.0f};

    auto hit = reg.test(LocalPoint{10.0f, 10.0f}, screen);
    ASSERT_TRUE(hit.has_value());
    // 兩個邊熱區皆命中，落回「同類別中較晚註冊者為準」——此處為 Top（後註冊）。
    EXPECT_EQ(hit->zone, EdgeHotZone::Top);
}

// -----------------------------------------------------------------------------
// 無效邊 / 無效厚度報錯不靜默
// -----------------------------------------------------------------------------

TEST(InvalidInput, UnknownZoneRejected) {
    EdgeHotZoneRegistry reg;
    const auto bogus = static_cast<EdgeHotZone>(99);
    EXPECT_FALSE(is_named_zone(bogus));
    EXPECT_EQ(reg.register_zone(bogus, 0.1f, "noop"), HotZoneStatus::InvalidZone);
    EXPECT_EQ(reg.size(), static_cast<std::size_t>(0));  // 不靜默略過：不會被註冊
}

TEST(InvalidInput, NonPositiveThicknessRejected) {
    EdgeHotZoneRegistry reg;
    EXPECT_EQ(reg.register_zone(EdgeHotZone::Left, 0.0f, "noop"),
              HotZoneStatus::InvalidThickness);
    EXPECT_EQ(reg.register_zone(EdgeHotZone::Left, -0.1f, "noop"),
              HotZoneStatus::InvalidThickness);
    EXPECT_EQ(reg.size(), static_cast<std::size_t>(0));
}

TEST(InvalidInput, ThicknessAboveOneRejected) {
    EdgeHotZoneRegistry reg;
    EXPECT_EQ(reg.register_zone(EdgeHotZone::Left, 1.1f, "noop"),
              HotZoneStatus::InvalidThickness);
    EXPECT_EQ(reg.size(), static_cast<std::size_t>(0));
}

TEST(InvalidInput, NonFiniteThicknessRejected) {
    EdgeHotZoneRegistry reg;
    EXPECT_EQ(reg.register_zone(EdgeHotZone::Left, kNaN, "noop"),
              HotZoneStatus::InvalidThickness);
    EXPECT_EQ(reg.size(), static_cast<std::size_t>(0));
}

TEST(InvalidInput, ValidRegistrationAfterRejectedOneStillWorks) {
    EdgeHotZoneRegistry reg;
    EXPECT_EQ(reg.register_zone(EdgeHotZone::Left, -1.0f, "bad"),
              HotZoneStatus::InvalidThickness);
    EXPECT_EQ(reg.register_zone(EdgeHotZone::Left, 0.1f, "good"), HotZoneStatus::Ok);
    EXPECT_EQ(reg.size(), static_cast<std::size_t>(1));

    ScreenExtent screen{1000.0f, 800.0f};
    auto hit = reg.test(LocalPoint{5.0f, 400.0f}, screen);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->action, "good");
}

// -----------------------------------------------------------------------------
// 不同螢幕尺寸
// -----------------------------------------------------------------------------

TEST(ScreenSize, RatioScalesWithDifferentScreenExtents) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::Left, 0.1f, "sidebar");

    // 小螢幕：熱區絕對寬度為 100 * 0.1 = 10。
    ScreenExtent small{100.0f, 100.0f};
    EXPECT_TRUE(reg.test(LocalPoint{9.0f, 50.0f}, small).has_value());
    EXPECT_FALSE(reg.test(LocalPoint{11.0f, 50.0f}, small).has_value());

    // 大螢幕（4K）：同一 ratio 換算出的絕對熱區寬度更大（3840 * 0.1 = 384）。
    ScreenExtent large{3840.0f, 2160.0f};
    EXPECT_TRUE(reg.test(LocalPoint{380.0f, 1000.0f}, large).has_value());
    EXPECT_FALSE(reg.test(LocalPoint{500.0f, 1000.0f}, large).has_value());
}

TEST(ScreenSize, InvalidScreenExtentYieldsNoTrigger) {
    EdgeHotZoneRegistry reg;
    reg.register_zone(EdgeHotZone::Left, 0.5f, "sidebar");

    EXPECT_FALSE(reg.test(LocalPoint{1.0f, 1.0f}, ScreenExtent{0.0f, 800.0f}).has_value());
    EXPECT_FALSE(reg.test(LocalPoint{1.0f, 1.0f}, ScreenExtent{-100.0f, 800.0f}).has_value());
    EXPECT_FALSE(reg.test(LocalPoint{1.0f, 1.0f}, ScreenExtent{kNaN, 800.0f}).has_value());
    EXPECT_FALSE(reg.test(LocalPoint{kNaN, 1.0f}, ScreenExtent{1000.0f, 800.0f}).has_value());
}

}  // namespace
