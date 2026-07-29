// E1-10 保持在螢幕內 — 單元測試（gtest）
//
// 驗證把 E1-07 anchor 解析出的 `ResolvedPlacement` 夾回 E1-18 具名螢幕可視範圍的行為：
//   - 完全在內 → 不動
//   - 超出右 / 下 / 左 / 上邊界 → 各軸獨立推回
//   - 角落同時超出（右下 / 左上等）→ 兩軸同時推回
//   - 元件大於螢幕（該軸）→ 貼齊起邊（0），不縮放、不裁切
//   - 用 E1-18 `ScreenRegistry` 的具名螢幕存在性 + 多螢幕情境（不同具名螢幕各自獨立夾回）
//   - 未知具名螢幕 / 非有限或負值尺寸 → Invalid（不靜默、不寫 out）
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實視窗 / 螢幕查詢 API。
#include "keep_on_screen.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

using ds::kernel::constrain;
using ds::kernel::is_within_screen;
using ds::kernel::KeepOnScreen;
using ds::kernel::KeepOnScreenStatus;
using ds::kernel::ResolvedPlacement;
using ds::kernel::Screen;
using ds::kernel::ScreenAnchor;
using ds::kernel::ScreenRegistry;
using ds::kernel::ScreenRole;
using ds::kernel::Size;

namespace {

constexpr float kEps = 1e-5f;

ResolvedPlacement make(float x, float y, float w, float h) {
    ResolvedPlacement p;
    p.x = x;
    p.y = y;
    p.width = w;
    p.height = h;
    return p;
}

// -----------------------------------------------------------------------------
// 完全在內 → 不動
// -----------------------------------------------------------------------------

TEST(Constrain, FullyInsideIsUnchanged) {
    const Size screen{1920.0f, 1080.0f};
    const ResolvedPlacement placement = make(100.0f, 100.0f, 200.0f, 150.0f);
    EXPECT_TRUE(is_within_screen(placement, screen));

    ResolvedPlacement out;
    ASSERT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Ok);
    EXPECT_NEAR(out.x, placement.x, kEps);
    EXPECT_NEAR(out.y, placement.y, kEps);
    EXPECT_NEAR(out.width, placement.width, kEps);
    EXPECT_NEAR(out.height, placement.height, kEps);
}

// -----------------------------------------------------------------------------
// 超出各邊界 → 推回
// -----------------------------------------------------------------------------

TEST(Constrain, ExceedsRightEdgeIsPushedBack) {
    const Size screen{800.0f, 600.0f};
    // 右緣超出：x + width = 900 > 800
    const ResolvedPlacement placement = make(750.0f, 100.0f, 150.0f, 100.0f);
    EXPECT_FALSE(is_within_screen(placement, screen));

    ResolvedPlacement out;
    ASSERT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Ok);
    EXPECT_NEAR(out.x, 650.0f, kEps);  // 800 - 150
    EXPECT_NEAR(out.y, 100.0f, kEps);  // y 軸未超出，不動
    EXPECT_NEAR(out.width, 150.0f, kEps);
    EXPECT_NEAR(out.height, 100.0f, kEps);
    EXPECT_TRUE(is_within_screen(out, screen));
}

TEST(Constrain, ExceedsBottomEdgeIsPushedBack) {
    const Size screen{800.0f, 600.0f};
    // 下緣超出：y + height = 650 > 600
    const ResolvedPlacement placement = make(100.0f, 500.0f, 150.0f, 150.0f);
    EXPECT_FALSE(is_within_screen(placement, screen));

    ResolvedPlacement out;
    ASSERT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Ok);
    EXPECT_NEAR(out.x, 100.0f, kEps);
    EXPECT_NEAR(out.y, 450.0f, kEps);  // 600 - 150
    EXPECT_TRUE(is_within_screen(out, screen));
}

TEST(Constrain, ExceedsLeftEdgeIsPushedBack) {
    const Size screen{800.0f, 600.0f};
    // 左緣超出：x < 0（例如靠左緣的彈窗因負向偏移越界）
    const ResolvedPlacement placement = make(-40.0f, 100.0f, 150.0f, 100.0f);
    EXPECT_FALSE(is_within_screen(placement, screen));

    ResolvedPlacement out;
    ASSERT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Ok);
    EXPECT_NEAR(out.x, 0.0f, kEps);
    EXPECT_NEAR(out.y, 100.0f, kEps);
    EXPECT_TRUE(is_within_screen(out, screen));
}

TEST(Constrain, ExceedsTopEdgeIsPushedBack) {
    const Size screen{800.0f, 600.0f};
    // 上緣超出：y < 0
    const ResolvedPlacement placement = make(100.0f, -25.0f, 150.0f, 100.0f);
    EXPECT_FALSE(is_within_screen(placement, screen));

    ResolvedPlacement out;
    ASSERT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Ok);
    EXPECT_NEAR(out.x, 100.0f, kEps);
    EXPECT_NEAR(out.y, 0.0f, kEps);
    EXPECT_TRUE(is_within_screen(out, screen));
}

// -----------------------------------------------------------------------------
// 角落同時超出 → 兩軸各自獨立推回
// -----------------------------------------------------------------------------

TEST(Constrain, BottomRightCornerBothAxesPushedBack) {
    const Size screen{1000.0f, 800.0f};
    const ResolvedPlacement placement = make(950.0f, 780.0f, 120.0f, 90.0f);
    EXPECT_FALSE(is_within_screen(placement, screen));

    ResolvedPlacement out;
    ASSERT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Ok);
    EXPECT_NEAR(out.x, 880.0f, kEps);  // 1000 - 120
    EXPECT_NEAR(out.y, 710.0f, kEps);  // 800 - 90
    EXPECT_TRUE(is_within_screen(out, screen));
}

TEST(Constrain, TopLeftCornerBothAxesPushedBack) {
    const Size screen{1000.0f, 800.0f};
    const ResolvedPlacement placement = make(-30.0f, -15.0f, 120.0f, 90.0f);
    EXPECT_FALSE(is_within_screen(placement, screen));

    ResolvedPlacement out;
    ASSERT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Ok);
    EXPECT_NEAR(out.x, 0.0f, kEps);
    EXPECT_NEAR(out.y, 0.0f, kEps);
    EXPECT_TRUE(is_within_screen(out, screen));
}

// -----------------------------------------------------------------------------
// 元件大於螢幕 → 貼齊起邊，不縮放、不裁切
// -----------------------------------------------------------------------------

TEST(Constrain, ElementWiderThanScreenClampsToOrigin) {
    const Size screen{800.0f, 600.0f};
    // 元件寬 900 > 螢幕寬 800：該軸不可能完全容納。
    const ResolvedPlacement placement = make(50.0f, 100.0f, 900.0f, 100.0f);

    ResolvedPlacement out;
    ASSERT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Ok);
    EXPECT_NEAR(out.x, 0.0f, kEps);       // 貼齊起邊
    EXPECT_NEAR(out.width, 900.0f, kEps);  // 尺寸不變（不縮放、不裁切）
    EXPECT_NEAR(out.y, 100.0f, kEps);      // y 軸未受影響
}

TEST(Constrain, ElementLargerThanScreenBothAxesClampsToOrigin) {
    const Size screen{800.0f, 600.0f};
    // 元件比螢幕大（兩軸皆超出容納能力）。
    const ResolvedPlacement placement = make(-100.0f, -50.0f, 2000.0f, 1500.0f);

    ResolvedPlacement out;
    ASSERT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Ok);
    EXPECT_NEAR(out.x, 0.0f, kEps);
    EXPECT_NEAR(out.y, 0.0f, kEps);
    EXPECT_NEAR(out.width, 2000.0f, kEps);
    EXPECT_NEAR(out.height, 1500.0f, kEps);
}

// -----------------------------------------------------------------------------
// 無效輸入 → Invalid（不靜默、不寫 out）
// -----------------------------------------------------------------------------

TEST(Constrain, NonFiniteScreenSizeIsInvalid) {
    const Size screen{std::numeric_limits<float>::infinity(), 600.0f};
    const ResolvedPlacement placement = make(0.0f, 0.0f, 10.0f, 10.0f);

    ResolvedPlacement out = make(999.0f, 999.0f, 999.0f, 999.0f);
    EXPECT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Invalid);
    // 不寫 out（不靜默）：保留呼叫端原值。
    EXPECT_NEAR(out.x, 999.0f, kEps);
}

TEST(Constrain, NegativeScreenSizeIsInvalid) {
    const Size screen{-800.0f, 600.0f};
    const ResolvedPlacement placement = make(0.0f, 0.0f, 10.0f, 10.0f);

    ResolvedPlacement out;
    EXPECT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Invalid);
}

TEST(Constrain, NonFinitePlacementSizeIsInvalid) {
    const Size screen{800.0f, 600.0f};
    const ResolvedPlacement placement =
        make(0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 10.0f);

    ResolvedPlacement out;
    EXPECT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Invalid);
}

TEST(Constrain, NegativePlacementSizeIsInvalid) {
    const Size screen{800.0f, 600.0f};
    const ResolvedPlacement placement = make(0.0f, 0.0f, -10.0f, 10.0f);

    ResolvedPlacement out;
    EXPECT_EQ(constrain(placement, screen, out), KeepOnScreenStatus::Invalid);
}

TEST(IsWithinScreen, InvalidInputsAreConservativelyFalse) {
    const Size bad_screen{-1.0f, 600.0f};
    const ResolvedPlacement placement = make(0.0f, 0.0f, 10.0f, 10.0f);
    EXPECT_FALSE(is_within_screen(placement, bad_screen));
}

// -----------------------------------------------------------------------------
// KeepOnScreen：以 E1-18 具名螢幕拓撲查詢 + 多螢幕情境
// -----------------------------------------------------------------------------

TEST(KeepOnScreenService, UsesE118DefaultsSinglePrimaryScreen) {
    const ScreenRegistry registry = ScreenRegistry::defaults();
    const KeepOnScreen service(registry);

    EXPECT_TRUE(service.knows_screen("screen.primary"));

    const Size screen_size{1440.0f, 900.0f};
    const ResolvedPlacement placement = make(1400.0f, 100.0f, 100.0f, 80.0f);  // 右緣超出

    ResolvedPlacement out;
    ASSERT_EQ(service.constrain_on("screen.primary", screen_size, placement, out),
              KeepOnScreenStatus::Ok);
    EXPECT_NEAR(out.x, 1340.0f, kEps);  // 1440 - 100
    EXPECT_TRUE(is_within_screen(out, screen_size));
}

TEST(KeepOnScreenService, UnknownNamedScreenIsInvalid) {
    const ScreenRegistry registry = ScreenRegistry::defaults();
    const KeepOnScreen service(registry);

    EXPECT_FALSE(service.knows_screen("screen.no-such-screen"));

    const Size screen_size{1440.0f, 900.0f};
    const ResolvedPlacement placement = make(0.0f, 0.0f, 100.0f, 80.0f);

    ResolvedPlacement out;
    EXPECT_EQ(service.constrain_on("screen.no-such-screen", screen_size, placement, out),
              KeepOnScreenStatus::Invalid);
}

// 多螢幕：每一具名螢幕各自獨立夾回（不同螢幕尺寸 → 不同夾回結果）。
TEST(KeepOnScreenService, MultiScreenEachClampedIndependently) {
    ScreenRegistry registry(std::vector<Screen>{
        {"screen.laptop", "內建", ScreenRole::Primary, ScreenAnchor::Center},
        {"screen.hdmi", "外接右", ScreenRole::Secondary, ScreenAnchor::Right},
    });
    const KeepOnScreen service(registry);

    ASSERT_TRUE(service.knows_screen("screen.laptop"));
    ASSERT_TRUE(service.knows_screen("screen.hdmi"));

    // 同一個 placement，套用到兩個不同尺寸的具名螢幕，應得到不同的夾回結果。
    const ResolvedPlacement placement = make(1850.0f, 50.0f, 100.0f, 80.0f);

    const Size laptop_size{1440.0f, 900.0f};
    ResolvedPlacement laptop_out;
    ASSERT_EQ(service.constrain_on("screen.laptop", laptop_size, placement, laptop_out),
              KeepOnScreenStatus::Ok);
    EXPECT_NEAR(laptop_out.x, 1340.0f, kEps);  // 1440 - 100
    EXPECT_TRUE(is_within_screen(laptop_out, laptop_size));

    const Size hdmi_size{3840.0f, 2160.0f};
    ResolvedPlacement hdmi_out;
    ASSERT_EQ(service.constrain_on("screen.hdmi", hdmi_size, placement, hdmi_out),
              KeepOnScreenStatus::Ok);
    // 3840 螢幕上該 placement 本已在內，不動。
    EXPECT_NEAR(hdmi_out.x, placement.x, kEps);
    EXPECT_NEAR(hdmi_out.y, placement.y, kEps);
    EXPECT_TRUE(is_within_screen(hdmi_out, hdmi_size));

    EXPECT_NE(laptop_out.x, hdmi_out.x);
}

}  // namespace
