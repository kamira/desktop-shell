// E1-07 anchor 定位模型 — 單元測試（gtest）
//
// 驗證以**具名錨點 + 相對偏移**表達定位、並在給定容器尺寸下解析為具體佈局的正確性：
//   - 九宮具名 anchor：正規化分數座標正確
//   - 相對偏移：正規化分數偏移 + 具名間距 Spacing + inset_from 內縮
//   - resolve 在不同容器尺寸下正確（比例縮放）
//   - 邊緣對齊：角 / 邊錨點使元件貼齊對應邊緣、置中錨點置中
//   - 無效 anchor / 非有限 offset / 非有限或負尺寸 → Invalid（不靜默、不寫 out）
//   - NFR-02 具名表達：核心以具名 Anchor + 正規化偏移承載，無絕對像素座標
//   - AnchorLayout：具名 SurfaceId 配對、就地更新、移除、未知 id 結構化回報
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實視窗 / 繪圖 API。
#include "anchor_model.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using ds::kernel::Anchor;
using ds::kernel::anchor_fraction;
using ds::kernel::AnchorLayout;
using ds::kernel::AnchorSpec;
using ds::kernel::AnchorStatus;
using ds::kernel::inset_from;
using ds::kernel::is_finite_size;
using ds::kernel::is_valid_anchor;
using ds::kernel::Offset;
using ds::kernel::resolve;
using ds::kernel::resolve_point;
using ds::kernel::ResolvedPlacement;
using ds::kernel::Size;
using ds::kernel::Spacing;

namespace {

constexpr float kEps = 1e-5f;

// -----------------------------------------------------------------------------
// 九宮具名 anchor：分數座標
// -----------------------------------------------------------------------------

TEST(Anchor, NineGridFractionsAreNamedNotPixels) {
    struct Case {
        Anchor a;
        float fx;
        float fy;
    };
    const Case cases[] = {
        {Anchor::TopLeft, 0.0f, 0.0f},     {Anchor::TopCenter, 0.5f, 0.0f},
        {Anchor::TopRight, 1.0f, 0.0f},    {Anchor::CenterLeft, 0.0f, 0.5f},
        {Anchor::Center, 0.5f, 0.5f},      {Anchor::CenterRight, 1.0f, 0.5f},
        {Anchor::BottomLeft, 0.0f, 1.0f},  {Anchor::BottomCenter, 0.5f, 1.0f},
        {Anchor::BottomRight, 1.0f, 1.0f},
    };
    for (const auto& c : cases) {
        float fx = -1.0f;
        float fy = -1.0f;
        ASSERT_TRUE(anchor_fraction(c.a, fx, fy));
        EXPECT_NEAR(fx, c.fx, kEps);
        EXPECT_NEAR(fy, c.fy, kEps);
    }
}

TEST(Anchor, ValidEnumRangeIsNineGrid) {
    for (int v = 0; v <= 8; ++v) {
        EXPECT_TRUE(is_valid_anchor(static_cast<Anchor>(v)));
    }
}

// -----------------------------------------------------------------------------
// 無效 anchor：報錯不靜默
// -----------------------------------------------------------------------------

TEST(Anchor, OutOfRangeAnchorIsInvalid) {
    EXPECT_FALSE(is_valid_anchor(static_cast<Anchor>(9)));
    EXPECT_FALSE(is_valid_anchor(static_cast<Anchor>(-1)));
    EXPECT_FALSE(is_valid_anchor(static_cast<Anchor>(255)));
}

TEST(Anchor, OutOfRangeAnchorFractionReturnsFalseAndLeavesOutUntouched) {
    float fx = 42.0f;
    float fy = 43.0f;
    EXPECT_FALSE(anchor_fraction(static_cast<Anchor>(99), fx, fy));
    // 不靜默改寫 out。
    EXPECT_NEAR(fx, 42.0f, kEps);
    EXPECT_NEAR(fy, 43.0f, kEps);
}

TEST(Resolve, OutOfRangeAnchorResolvesInvalid) {
    AnchorSpec spec;
    spec.anchor = static_cast<Anchor>(50);
    ResolvedPlacement out;
    out.x = 7.0f;
    EXPECT_EQ(resolve(spec, Size{100.0f, 100.0f}, Size{10.0f, 10.0f}, out), AnchorStatus::Invalid);
    EXPECT_NEAR(out.x, 7.0f, kEps);  // out 未被觸碰
}

// -----------------------------------------------------------------------------
// resolve：邊緣對齊 + 置中（元件貼齊、不溢出）
// -----------------------------------------------------------------------------

TEST(Resolve, CornerAnchorsAlignToEdges) {
    const Size container{200.0f, 100.0f};
    const Size element{40.0f, 20.0f};
    ResolvedPlacement out;

    ASSERT_EQ(resolve({Anchor::TopLeft, {}}, container, element, out), AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 0.0f, kEps);
    EXPECT_NEAR(out.y, 0.0f, kEps);

    ASSERT_EQ(resolve({Anchor::BottomRight, {}}, container, element, out), AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 160.0f, kEps);  // 200 - 40
    EXPECT_NEAR(out.y, 80.0f, kEps);   // 100 - 20

    ASSERT_EQ(resolve({Anchor::TopRight, {}}, container, element, out), AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 160.0f, kEps);
    EXPECT_NEAR(out.y, 0.0f, kEps);

    ASSERT_EQ(resolve({Anchor::BottomLeft, {}}, container, element, out), AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 0.0f, kEps);
    EXPECT_NEAR(out.y, 80.0f, kEps);
}

TEST(Resolve, CenterAnchorCentersElement) {
    const Size container{200.0f, 100.0f};
    const Size element{40.0f, 20.0f};
    ResolvedPlacement out;
    ASSERT_EQ(resolve({Anchor::Center, {}}, container, element, out), AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 80.0f, kEps);  // (200 - 40) / 2
    EXPECT_NEAR(out.y, 40.0f, kEps);  // (100 - 20) / 2
    EXPECT_NEAR(out.width, 40.0f, kEps);
    EXPECT_NEAR(out.height, 20.0f, kEps);
}

TEST(Resolve, EdgeMidAnchorsAlignOnOneAxisCenterOnOther) {
    const Size container{200.0f, 100.0f};
    const Size element{40.0f, 20.0f};
    ResolvedPlacement out;
    ASSERT_EQ(resolve({Anchor::TopCenter, {}}, container, element, out), AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 80.0f, kEps);  // 水平置中
    EXPECT_NEAR(out.y, 0.0f, kEps);   // 貼上緣
    ASSERT_EQ(resolve({Anchor::CenterRight, {}}, container, element, out), AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 160.0f, kEps);  // 貼右緣
    EXPECT_NEAR(out.y, 40.0f, kEps);   // 垂直置中
}

// -----------------------------------------------------------------------------
// resolve：不同容器尺寸下的比例縮放
// -----------------------------------------------------------------------------

TEST(Resolve, ScalesWithDifferentContainerSizes) {
    const Size element{10.0f, 10.0f};
    ResolvedPlacement small;
    ResolvedPlacement large;
    ASSERT_EQ(resolve({Anchor::BottomRight, {}}, Size{100.0f, 100.0f}, element, small),
              AnchorStatus::Ok);
    ASSERT_EQ(resolve({Anchor::BottomRight, {}}, Size{1000.0f, 500.0f}, element, large),
              AnchorStatus::Ok);
    EXPECT_NEAR(small.x, 90.0f, kEps);
    EXPECT_NEAR(small.y, 90.0f, kEps);
    EXPECT_NEAR(large.x, 990.0f, kEps);
    EXPECT_NEAR(large.y, 490.0f, kEps);
}

TEST(Resolve, PointResolveTreatsElementAsZeroSize) {
    float x = 0.0f;
    float y = 0.0f;
    ASSERT_EQ(resolve_point({Anchor::BottomRight, {}}, Size{300.0f, 200.0f}, x, y),
              AnchorStatus::Ok);
    EXPECT_NEAR(x, 300.0f, kEps);  // 錨點就在右下角
    EXPECT_NEAR(y, 200.0f, kEps);
    ASSERT_EQ(resolve_point({Anchor::Center, {}}, Size{300.0f, 200.0f}, x, y), AnchorStatus::Ok);
    EXPECT_NEAR(x, 150.0f, kEps);
    EXPECT_NEAR(y, 100.0f, kEps);
}

// -----------------------------------------------------------------------------
// 相對偏移：正規化分數 + 具名間距
// -----------------------------------------------------------------------------

TEST(Offset, NormalizedFractionOffsetIsRelativeToContainer) {
    const Size container{500.0f, 400.0f};
    const Size element{0.0f, 0.0f};
    ResolvedPlacement out;
    // TopLeft + 偏移 (0.1, 0.25) → (50, 100)：偏移是容器尺寸的比例，非像素。
    ASSERT_EQ(resolve({Anchor::TopLeft, Offset{0.1f, 0.25f}}, container, element, out),
              AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 50.0f, kEps);
    EXPECT_NEAR(out.y, 100.0f, kEps);
    // 同偏移在更大容器 → 依比例放大。
    ASSERT_EQ(resolve({Anchor::TopLeft, Offset{0.1f, 0.25f}}, Size{1000.0f, 800.0f}, element, out),
              AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 100.0f, kEps);
    EXPECT_NEAR(out.y, 200.0f, kEps);
}

TEST(Offset, NamedSpacingMapsToNormalizedFraction) {
    EXPECT_NEAR(ds::kernel::spacing_fraction(Spacing::None), 0.0f, kEps);
    EXPECT_GT(ds::kernel::spacing_fraction(Spacing::Tight), 0.0f);
    // 具名等級遞增。
    EXPECT_LT(ds::kernel::spacing_fraction(Spacing::Tight),
              ds::kernel::spacing_fraction(Spacing::Snug));
    EXPECT_LT(ds::kernel::spacing_fraction(Spacing::Snug),
              ds::kernel::spacing_fraction(Spacing::Cozy));
    EXPECT_LT(ds::kernel::spacing_fraction(Spacing::Cozy),
              ds::kernel::spacing_fraction(Spacing::Roomy));
    EXPECT_LT(ds::kernel::spacing_fraction(Spacing::Roomy),
              ds::kernel::spacing_fraction(Spacing::Spacious));
}

TEST(Offset, FromSpacingBuildsOffsetFromNamedLevels) {
    const Offset o = Offset::from_spacing(Spacing::Cozy, Spacing::None);
    EXPECT_NEAR(o.dx, ds::kernel::spacing_fraction(Spacing::Cozy), kEps);
    EXPECT_NEAR(o.dy, 0.0f, kEps);
}

TEST(Offset, InsetFromMovesInwardFromAnchoredEdge) {
    const float s = ds::kernel::spacing_fraction(Spacing::Roomy);
    // 左上：向內 = 右下（+,+）。
    Offset o = inset_from(Anchor::TopLeft, Spacing::Roomy);
    EXPECT_NEAR(o.dx, s, kEps);
    EXPECT_NEAR(o.dy, s, kEps);
    // 右下：向內 = 左上（-,-）。
    o = inset_from(Anchor::BottomRight, Spacing::Roomy);
    EXPECT_NEAR(o.dx, -s, kEps);
    EXPECT_NEAR(o.dy, -s, kEps);
    // 置中：兩軸皆不動。
    o = inset_from(Anchor::Center, Spacing::Roomy);
    EXPECT_NEAR(o.dx, 0.0f, kEps);
    EXPECT_NEAR(o.dy, 0.0f, kEps);
    // 上緣中央：僅垂直向內（下），水平不動。
    o = inset_from(Anchor::TopCenter, Spacing::Roomy);
    EXPECT_NEAR(o.dx, 0.0f, kEps);
    EXPECT_NEAR(o.dy, s, kEps);
}

TEST(Resolve, InsetKeepsElementJustInsideAnchoredCorner) {
    const Size container{1000.0f, 1000.0f};
    const Size element{100.0f, 100.0f};
    ResolvedPlacement out;
    // 右下錨 + 由該角內縮 Snug（0.02 → 20px）。
    const AnchorSpec spec{Anchor::BottomRight, inset_from(Anchor::BottomRight, Spacing::Snug)};
    ASSERT_EQ(resolve(spec, container, element, out), AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 900.0f - 20.0f, kEps);  // 貼齊 900 再內縮 20
    EXPECT_NEAR(out.y, 900.0f - 20.0f, kEps);
}

// -----------------------------------------------------------------------------
// 非有限 / 負尺寸 / 非有限偏移：Invalid（不靜默）
// -----------------------------------------------------------------------------

TEST(Resolve, NonFiniteOffsetIsInvalid) {
    ResolvedPlacement out;
    out.x = 5.0f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(resolve({Anchor::Center, Offset{nan, 0.0f}}, Size{10.0f, 10.0f}, Size{1.0f, 1.0f},
                      out),
              AnchorStatus::Invalid);
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_EQ(resolve({Anchor::Center, Offset{0.0f, inf}}, Size{10.0f, 10.0f}, Size{1.0f, 1.0f},
                      out),
              AnchorStatus::Invalid);
    EXPECT_NEAR(out.x, 5.0f, kEps);  // out 未被觸碰
}

TEST(Resolve, NonFiniteOrNegativeSizeIsInvalid) {
    ResolvedPlacement out;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_EQ(resolve({Anchor::Center, {}}, Size{nan, 10.0f}, Size{1.0f, 1.0f}, out),
              AnchorStatus::Invalid);
    EXPECT_EQ(resolve({Anchor::Center, {}}, Size{10.0f, inf}, Size{1.0f, 1.0f}, out),
              AnchorStatus::Invalid);
    EXPECT_EQ(resolve({Anchor::Center, {}}, Size{10.0f, 10.0f}, Size{-1.0f, 1.0f}, out),
              AnchorStatus::Invalid);
    EXPECT_EQ(resolve({Anchor::Center, {}}, Size{-10.0f, 10.0f}, Size{1.0f, 1.0f}, out),
              AnchorStatus::Invalid);
}

TEST(Resolve, IsFiniteSizeHelper) {
    EXPECT_TRUE(is_finite_size(Size{0.0f, 0.0f}));
    EXPECT_TRUE(is_finite_size(Size{100.0f, 50.0f}));
    EXPECT_FALSE(is_finite_size(Size{-1.0f, 0.0f}));
    EXPECT_FALSE(is_finite_size(Size{0.0f, std::numeric_limits<float>::quiet_NaN()}));
}

// -----------------------------------------------------------------------------
// NFR-02：具名表達（無絕對像素座標於核心 spec）
// -----------------------------------------------------------------------------

TEST(Nfr02, PositionExpressedByNamedAnchorAndRelativeOffset) {
    // 相同的具名定位規格（右下角、內縮 Cozy）套到兩個不同尺寸的容器，
    // 皆解析到「貼右下、依比例內縮」的位置——證明定位以具名 + 相對表達，不硬編像素。
    const AnchorSpec spec{Anchor::BottomRight, inset_from(Anchor::BottomRight, Spacing::Cozy)};
    const Size element{50.0f, 50.0f};
    ResolvedPlacement a;
    ResolvedPlacement b;
    ASSERT_EQ(resolve(spec, Size{500.0f, 500.0f}, element, a), AnchorStatus::Ok);
    ASSERT_EQ(resolve(spec, Size{1000.0f, 1000.0f}, element, b), AnchorStatus::Ok);
    const float cozy = ds::kernel::spacing_fraction(Spacing::Cozy);
    EXPECT_NEAR(a.x, (500.0f - 50.0f) - cozy * 500.0f, kEps);
    EXPECT_NEAR(b.x, (1000.0f - 50.0f) - cozy * 1000.0f, kEps);
    // 兩者的「距右緣間距 / 容器寬」比例一致（相對定位的本質）。
    const float gap_a = (a.x + element.width);  // 元件右緣 x
    const float gap_b = (b.x + element.width);
    EXPECT_NEAR((500.0f - gap_a) / 500.0f, (1000.0f - gap_b) / 1000.0f, kEps);
}

// -----------------------------------------------------------------------------
// AnchorLayout：具名 surface 配對
// -----------------------------------------------------------------------------

TEST(AnchorLayout, PlaceAndResolveByNamedSurfaceId) {
    AnchorLayout layout;
    EXPECT_EQ(layout.placement_count(), 0u);
    EXPECT_EQ(layout.place("surface.pet", {Anchor::BottomRight, {}}), AnchorStatus::Ok);
    EXPECT_EQ(layout.place("surface.panel", {Anchor::TopCenter, {}}), AnchorStatus::Ok);
    EXPECT_EQ(layout.placement_count(), 2u);
    EXPECT_TRUE(layout.has_placement("surface.pet"));
    ASSERT_NE(layout.spec_of("surface.pet"), nullptr);
    EXPECT_EQ(layout.spec_of("surface.pet")->anchor, Anchor::BottomRight);

    ResolvedPlacement out;
    ASSERT_EQ(layout.resolve_for("surface.pet", Size{800.0f, 600.0f}, Size{80.0f, 80.0f}, out),
              AnchorStatus::Ok);
    EXPECT_NEAR(out.x, 720.0f, kEps);
    EXPECT_NEAR(out.y, 520.0f, kEps);
}

TEST(AnchorLayout, PlaceUpdatesInPlaceForSameId) {
    AnchorLayout layout;
    ASSERT_EQ(layout.place("surface.pet", {Anchor::TopLeft, {}}), AnchorStatus::Ok);
    ASSERT_EQ(layout.place("surface.pet", {Anchor::BottomRight, {}}), AnchorStatus::Ok);
    EXPECT_EQ(layout.placement_count(), 1u);  // 不新增第二筆
    ASSERT_NE(layout.spec_of("surface.pet"), nullptr);
    EXPECT_EQ(layout.spec_of("surface.pet")->anchor, Anchor::BottomRight);
}

TEST(AnchorLayout, EmptyIdOrInvalidSpecRejected) {
    AnchorLayout layout;
    EXPECT_EQ(layout.place("", {Anchor::Center, {}}), AnchorStatus::Invalid);
    AnchorSpec bad;
    bad.anchor = static_cast<Anchor>(77);
    EXPECT_EQ(layout.place("surface.x", bad), AnchorStatus::Invalid);
    AnchorSpec badoff{Anchor::Center, Offset{std::numeric_limits<float>::infinity(), 0.0f}};
    EXPECT_EQ(layout.place("surface.y", badoff), AnchorStatus::Invalid);
    EXPECT_EQ(layout.placement_count(), 0u);
}

TEST(AnchorLayout, RemoveAndUnknownIdStructured) {
    AnchorLayout layout;
    ASSERT_EQ(layout.place("surface.a", {Anchor::Center, {}}), AnchorStatus::Ok);
    EXPECT_EQ(layout.remove("surface.a"), AnchorStatus::Ok);
    EXPECT_FALSE(layout.has_placement("surface.a"));
    EXPECT_EQ(layout.placement_count(), 0u);
    // 未知 id：不崩潰，結構化回 Invalid。
    EXPECT_EQ(layout.remove("surface.ghost"), AnchorStatus::Invalid);
    EXPECT_EQ(layout.spec_of("surface.ghost"), nullptr);
    ResolvedPlacement out;
    EXPECT_EQ(layout.resolve_for("surface.ghost", Size{10.0f, 10.0f}, Size{1.0f, 1.0f}, out),
              AnchorStatus::Invalid);
}

}  // namespace
