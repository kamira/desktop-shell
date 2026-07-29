// E1-11 從屬 surface 相對定位 — 單元測試（gtest）
//
// 驗證子(child) surface 相對父(parent) surface 以 E1-07 具名 anchor + 相對偏移定位的正確性：
//   - 附著子到父 + anchor：attach 記錄、查詢 parent_of / spec_of / is_attached
//   - resolve_child：子佈局隨父已解析矩形（container = 父矩形）正確計算並平移
//   - 父移動子跟隨：同一附著在不同 parent_placement 下重新解析，子位置隨之改變（不快取絕對座標）
//   - detach：移除單一附著（不影響其餘）、未知 child 結構化回報
//   - reposition：更新既有附著的定位規格，parent 不變
//   - 父關閉子連帶：close_parent 遞迴清除該 parent 下所有直接 / 間接子附著
//   - 循環 / 自附：報錯不靜默（Invalid），不記錄
//   - 多子並存：同一 parent 下多個 child 互不干擾
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實視窗 / 繪圖 API。
#include "subordinate_layout.hpp"

#include <gtest/gtest.h>

#include <limits>

using ds::kernel::Anchor;
using ds::kernel::AnchorSpec;
using ds::kernel::AnchorStatus;
using ds::kernel::Offset;
using ds::kernel::ResolvedPlacement;
using ds::kernel::Size;
using ds::kernel::SubordinateLayout;

namespace {

constexpr float kEps = 1e-5f;

// -----------------------------------------------------------------------------
// 附著子到父 + anchor
// -----------------------------------------------------------------------------

TEST(Attach, AttachesChildToParentWithAnchorSpec) {
    SubordinateLayout layout;
    EXPECT_EQ(layout.attachment_count(), 0u);
    EXPECT_FALSE(layout.is_attached("surface.tooltip"));

    const AnchorSpec spec{Anchor::BottomLeft, Offset{0.0f, 0.02f}};
    EXPECT_EQ(layout.attach("surface.tooltip", "surface.button", spec), AnchorStatus::Ok);

    EXPECT_TRUE(layout.is_attached("surface.tooltip"));
    EXPECT_EQ(layout.attachment_count(), 1u);
    ASSERT_NE(layout.parent_of("surface.tooltip"), nullptr);
    EXPECT_EQ(*layout.parent_of("surface.tooltip"), "surface.button");
    ASSERT_NE(layout.spec_of("surface.tooltip"), nullptr);
    EXPECT_EQ(layout.spec_of("surface.tooltip")->anchor, Anchor::BottomLeft);
    EXPECT_TRUE(layout.has_children("surface.button"));
}

TEST(Attach, ReattachSameChildUpdatesInPlace) {
    SubordinateLayout layout;
    ASSERT_EQ(layout.attach("surface.menu", "surface.panel", {Anchor::TopLeft, {}}),
              AnchorStatus::Ok);
    ASSERT_EQ(layout.attach("surface.menu", "surface.other", {Anchor::BottomRight, {}}),
              AnchorStatus::Ok);
    EXPECT_EQ(layout.attachment_count(), 1u);  // 不新增第二筆
    ASSERT_NE(layout.parent_of("surface.menu"), nullptr);
    EXPECT_EQ(*layout.parent_of("surface.menu"), "surface.other");
    ASSERT_NE(layout.spec_of("surface.menu"), nullptr);
    EXPECT_EQ(layout.spec_of("surface.menu")->anchor, Anchor::BottomRight);
}

TEST(Attach, EmptyIdsOrInvalidSpecRejected) {
    SubordinateLayout layout;
    EXPECT_EQ(layout.attach("", "surface.parent", {Anchor::Center, {}}), AnchorStatus::Invalid);
    EXPECT_EQ(layout.attach("surface.child", "", {Anchor::Center, {}}), AnchorStatus::Invalid);

    AnchorSpec bad_anchor;
    bad_anchor.anchor = static_cast<Anchor>(99);
    EXPECT_EQ(layout.attach("surface.child", "surface.parent", bad_anchor), AnchorStatus::Invalid);

    const AnchorSpec bad_offset{Anchor::Center,
                                Offset{std::numeric_limits<float>::infinity(), 0.0f}};
    EXPECT_EQ(layout.attach("surface.child", "surface.parent", bad_offset), AnchorStatus::Invalid);

    EXPECT_EQ(layout.attachment_count(), 0u);
}

// -----------------------------------------------------------------------------
// resolve_child：子佈局隨父已解析矩形計算
// -----------------------------------------------------------------------------

TEST(ResolveChild, ComputesAbsolutePlacementRelativeToParentRect) {
    SubordinateLayout layout;
    // 子貼齊父的右下角。
    ASSERT_EQ(layout.attach("surface.badge", "surface.icon", {Anchor::BottomRight, {}}),
              AnchorStatus::Ok);

    const ResolvedPlacement parent{/*x=*/100.0f, /*y=*/200.0f, /*width=*/40.0f, /*height=*/40.0f};
    const Size child_element{10.0f, 10.0f};
    ResolvedPlacement out;
    ASSERT_EQ(layout.resolve_child("surface.badge", parent, child_element, out), AnchorStatus::Ok);

    // 局部：BottomRight 貼齊 (40-10, 40-10) = (30, 30)；平移 (+100, +200) → (130, 230)。
    EXPECT_NEAR(out.x, 130.0f, kEps);
    EXPECT_NEAR(out.y, 230.0f, kEps);
    EXPECT_NEAR(out.width, 10.0f, kEps);
    EXPECT_NEAR(out.height, 10.0f, kEps);
}

TEST(ResolveChild, OffsetExtendsBeyondParentBoundsForDropdownStylePlacement) {
    SubordinateLayout layout;
    // 典型下拉選單：貼父的左下角，並向下偏移一點（超出父矩形範圍，非父的內部子區域）。
    const AnchorSpec spec{Anchor::BottomLeft, Offset{0.0f, 0.05f}};
    ASSERT_EQ(layout.attach("surface.dropdown", "surface.combo", spec), AnchorStatus::Ok);

    const ResolvedPlacement parent{50.0f, 50.0f, 100.0f, 20.0f};
    const Size child_element{100.0f, 60.0f};
    ResolvedPlacement out;
    ASSERT_EQ(layout.resolve_child("surface.dropdown", parent, child_element, out),
              AnchorStatus::Ok);

    // 局部 x = 0（貼左），y = (20-60) + 0.05*20 = -40 + 1 = -39；平移 (+50, +50)。
    EXPECT_NEAR(out.x, 50.0f, kEps);
    EXPECT_NEAR(out.y, 50.0f - 39.0f, kEps);
}

TEST(ResolveChild, UnknownChildIsInvalidAndDoesNotWriteOut) {
    SubordinateLayout layout;
    ResolvedPlacement out;
    out.x = 7.0f;
    EXPECT_EQ(layout.resolve_child("surface.ghost", ResolvedPlacement{0, 0, 10, 10},
                                   Size{1.0f, 1.0f}, out),
              AnchorStatus::Invalid);
    EXPECT_NEAR(out.x, 7.0f, kEps);  // out 未被觸碰
}

TEST(ResolveChild, NonFiniteParentOrElementSizeIsInvalid) {
    SubordinateLayout layout;
    ASSERT_EQ(layout.attach("surface.tip", "surface.host", {Anchor::Center, {}}),
              AnchorStatus::Ok);
    ResolvedPlacement out;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(layout.resolve_child("surface.tip", ResolvedPlacement{0, 0, nan, 10},
                                   Size{1.0f, 1.0f}, out),
              AnchorStatus::Invalid);
    EXPECT_EQ(layout.resolve_child("surface.tip", ResolvedPlacement{0, 0, 10, 10},
                                   Size{-1.0f, 1.0f}, out),
              AnchorStatus::Invalid);
}

// -----------------------------------------------------------------------------
// 父移動子跟隨：不快取絕對座標，重新解析即得新位置
// -----------------------------------------------------------------------------

TEST(ResolveChild, ChildFollowsWhenParentRectChanges) {
    SubordinateLayout layout;
    ASSERT_EQ(layout.attach("surface.tip", "surface.button", {Anchor::TopCenter, {}}),
              AnchorStatus::Ok);
    const Size child_element{20.0f, 10.0f};

    ResolvedPlacement before;
    ASSERT_EQ(layout.resolve_child("surface.tip", ResolvedPlacement{0.0f, 0.0f, 60.0f, 30.0f},
                                   child_element, before),
              AnchorStatus::Ok);
    EXPECT_NEAR(before.x, 20.0f, kEps);  // (60-20)/2
    EXPECT_NEAR(before.y, 0.0f, kEps);

    // 父移動到新位置（同尺寸，不同原點）——子重新解析後應整體平移相同量。
    ResolvedPlacement after;
    ASSERT_EQ(layout.resolve_child("surface.tip", ResolvedPlacement{300.0f, 500.0f, 60.0f, 30.0f},
                                   child_element, after),
              AnchorStatus::Ok);
    EXPECT_NEAR(after.x, 20.0f + 300.0f, kEps);
    EXPECT_NEAR(after.y, 0.0f + 500.0f, kEps);

    // 父同時改變尺寸——子局部位置依比例重新計算，非單純平移。
    ResolvedPlacement resized;
    ASSERT_EQ(layout.resolve_child("surface.tip", ResolvedPlacement{300.0f, 500.0f, 200.0f, 30.0f},
                                   child_element, resized),
              AnchorStatus::Ok);
    EXPECT_NEAR(resized.x, 300.0f + (200.0f - 20.0f) / 2.0f, kEps);
    EXPECT_NEAR(resized.y, 500.0f, kEps);
}

// -----------------------------------------------------------------------------
// detach
// -----------------------------------------------------------------------------

TEST(Detach, RemovesOnlyTargetAttachment) {
    SubordinateLayout layout;
    ASSERT_EQ(layout.attach("surface.a", "surface.parent", {Anchor::Center, {}}),
              AnchorStatus::Ok);
    ASSERT_EQ(layout.attach("surface.b", "surface.parent", {Anchor::Center, {}}),
              AnchorStatus::Ok);
    EXPECT_EQ(layout.detach("surface.a"), AnchorStatus::Ok);
    EXPECT_FALSE(layout.is_attached("surface.a"));
    EXPECT_TRUE(layout.is_attached("surface.b"));  // 不受影響
    EXPECT_EQ(layout.attachment_count(), 1u);
}

TEST(Detach, UnknownChildIsInvalidNotCrash) {
    SubordinateLayout layout;
    EXPECT_EQ(layout.detach("surface.ghost"), AnchorStatus::Invalid);
}

// -----------------------------------------------------------------------------
// reposition：更新定位規格，parent 不變
// -----------------------------------------------------------------------------

TEST(Reposition, UpdatesSpecKeepsParent) {
    SubordinateLayout layout;
    ASSERT_EQ(layout.attach("surface.tip", "surface.host", {Anchor::TopLeft, {}}),
              AnchorStatus::Ok);
    EXPECT_EQ(layout.reposition("surface.tip", {Anchor::BottomRight, Offset{0.01f, 0.01f}}),
              AnchorStatus::Ok);
    ASSERT_NE(layout.spec_of("surface.tip"), nullptr);
    EXPECT_EQ(layout.spec_of("surface.tip")->anchor, Anchor::BottomRight);
    ASSERT_NE(layout.parent_of("surface.tip"), nullptr);
    EXPECT_EQ(*layout.parent_of("surface.tip"), "surface.host");  // parent 不變
}

TEST(Reposition, UnknownChildOrInvalidSpecRejected) {
    SubordinateLayout layout;
    EXPECT_EQ(layout.reposition("surface.ghost", {Anchor::Center, {}}), AnchorStatus::Invalid);

    ASSERT_EQ(layout.attach("surface.tip", "surface.host", {Anchor::TopLeft, {}}),
              AnchorStatus::Ok);
    AnchorSpec bad;
    bad.anchor = static_cast<Anchor>(-5);
    EXPECT_EQ(layout.reposition("surface.tip", bad), AnchorStatus::Invalid);
    // 未變更。
    ASSERT_NE(layout.spec_of("surface.tip"), nullptr);
    EXPECT_EQ(layout.spec_of("surface.tip")->anchor, Anchor::TopLeft);
}

// -----------------------------------------------------------------------------
// 父關閉子連帶
// -----------------------------------------------------------------------------

TEST(CloseParent, CascadesDirectChildren) {
    SubordinateLayout layout;
    ASSERT_EQ(layout.attach("surface.tip1", "surface.panel", {Anchor::Center, {}}),
              AnchorStatus::Ok);
    ASSERT_EQ(layout.attach("surface.tip2", "surface.panel", {Anchor::Center, {}}),
              AnchorStatus::Ok);
    EXPECT_EQ(layout.attachment_count(), 2u);

    const std::size_t removed = layout.close_parent("surface.panel");
    EXPECT_EQ(removed, 2u);
    EXPECT_FALSE(layout.is_attached("surface.tip1"));
    EXPECT_FALSE(layout.is_attached("surface.tip2"));
    EXPECT_EQ(layout.attachment_count(), 0u);
}

TEST(CloseParent, CascadesTransitiveDescendantsAndSelf) {
    SubordinateLayout layout;
    // panel -> dropdown -> submenu -> subsubmenu（巢狀從屬鏈）。
    ASSERT_EQ(layout.attach("surface.dropdown", "surface.panel", {Anchor::Center, {}}),
              AnchorStatus::Ok);
    ASSERT_EQ(layout.attach("surface.submenu", "surface.dropdown", {Anchor::Center, {}}),
              AnchorStatus::Ok);
    ASSERT_EQ(layout.attach("surface.subsubmenu", "surface.submenu", {Anchor::Center, {}}),
              AnchorStatus::Ok);
    // 一個與此鏈無關的附著，應不受影響。
    ASSERT_EQ(layout.attach("surface.unrelated", "surface.other", {Anchor::Center, {}}),
              AnchorStatus::Ok);
    EXPECT_EQ(layout.attachment_count(), 4u);

    // 關閉中段的 dropdown：連帶清除 dropdown 自身 + submenu + subsubmenu，不影響 unrelated。
    const std::size_t removed = layout.close_parent("surface.dropdown");
    EXPECT_EQ(removed, 3u);
    EXPECT_FALSE(layout.is_attached("surface.dropdown"));
    EXPECT_FALSE(layout.is_attached("surface.submenu"));
    EXPECT_FALSE(layout.is_attached("surface.subsubmenu"));
    EXPECT_TRUE(layout.is_attached("surface.unrelated"));
    EXPECT_EQ(layout.attachment_count(), 1u);
}

TEST(CloseParent, ClosingIdWithNoAttachmentsRemovesZero) {
    SubordinateLayout layout;
    EXPECT_EQ(layout.close_parent("surface.nothing"), 0u);
}

// -----------------------------------------------------------------------------
// 循環 / 自附：報錯不靜默
// -----------------------------------------------------------------------------

TEST(Attach, SelfAttachRejected) {
    SubordinateLayout layout;
    EXPECT_EQ(layout.attach("surface.x", "surface.x", {Anchor::Center, {}}), AnchorStatus::Invalid);
    EXPECT_FALSE(layout.is_attached("surface.x"));
    EXPECT_EQ(layout.attachment_count(), 0u);
}

TEST(Attach, TwoNodeCycleRejected) {
    SubordinateLayout layout;
    ASSERT_EQ(layout.attach("surface.a", "surface.b", {Anchor::Center, {}}), AnchorStatus::Ok);
    // b 附著到 a 會形成 a<->b 循環：拒絕，且不變更既有的 a→b 附著。
    EXPECT_EQ(layout.attach("surface.b", "surface.a", {Anchor::Center, {}}), AnchorStatus::Invalid);
    EXPECT_EQ(layout.attachment_count(), 1u);
    EXPECT_FALSE(layout.is_attached("surface.b"));
}

TEST(Attach, MultiNodeCycleRejected) {
    SubordinateLayout layout;
    // a -> b -> c 建立鏈；c -> a 會形成三節點循環：拒絕。
    ASSERT_EQ(layout.attach("surface.a", "surface.b", {Anchor::Center, {}}), AnchorStatus::Ok);
    ASSERT_EQ(layout.attach("surface.b", "surface.c", {Anchor::Center, {}}), AnchorStatus::Ok);
    EXPECT_EQ(layout.attach("surface.c", "surface.a", {Anchor::Center, {}}), AnchorStatus::Invalid);
    EXPECT_EQ(layout.attachment_count(), 2u);
    EXPECT_FALSE(layout.is_attached("surface.c"));
}

TEST(Attach, ReattachCausingCycleViaExistingChainRejected) {
    SubordinateLayout layout;
    // panel <- dropdown <- submenu（submenu 的 parent 是 dropdown，dropdown 的 parent 是 panel）。
    ASSERT_EQ(layout.attach("surface.dropdown", "surface.panel", {Anchor::Center, {}}),
              AnchorStatus::Ok);
    ASSERT_EQ(layout.attach("surface.submenu", "surface.dropdown", {Anchor::Center, {}}),
              AnchorStatus::Ok);
    // 嘗試把 panel 重新附著到 submenu（其實際上是 panel 的孫）：形成循環，拒絕。
    EXPECT_EQ(layout.attach("surface.panel", "surface.submenu", {Anchor::Center, {}}),
              AnchorStatus::Invalid);
    EXPECT_FALSE(layout.is_attached("surface.panel"));
}

// -----------------------------------------------------------------------------
// 多子並存
// -----------------------------------------------------------------------------

TEST(MultipleChildren, CoexistIndependentlyUnderSameParent) {
    SubordinateLayout layout;
    ASSERT_EQ(layout.attach("surface.tip.left", "surface.toolbar", {Anchor::BottomLeft, {}}),
              AnchorStatus::Ok);
    ASSERT_EQ(layout.attach("surface.tip.right", "surface.toolbar", {Anchor::BottomRight, {}}),
              AnchorStatus::Ok);
    ASSERT_EQ(layout.attach("surface.tip.center", "surface.toolbar", {Anchor::BottomCenter, {}}),
              AnchorStatus::Ok);
    EXPECT_EQ(layout.attachment_count(), 3u);

    const ResolvedPlacement parent{0.0f, 0.0f, 300.0f, 30.0f};
    const Size element{20.0f, 10.0f};
    ResolvedPlacement left;
    ResolvedPlacement right;
    ResolvedPlacement center;
    ASSERT_EQ(layout.resolve_child("surface.tip.left", parent, element, left), AnchorStatus::Ok);
    ASSERT_EQ(layout.resolve_child("surface.tip.right", parent, element, right), AnchorStatus::Ok);
    ASSERT_EQ(layout.resolve_child("surface.tip.center", parent, element, center),
              AnchorStatus::Ok);

    EXPECT_NEAR(left.x, 0.0f, kEps);
    EXPECT_NEAR(right.x, 280.0f, kEps);    // 300 - 20
    EXPECT_NEAR(center.x, 140.0f, kEps);   // (300-20)/2
    // 各自獨立，互不覆寫。
    EXPECT_NE(left.x, right.x);
    EXPECT_NE(left.x, center.x);

    // 移除其中一個不影響其他兩個。
    EXPECT_EQ(layout.detach("surface.tip.right"), AnchorStatus::Ok);
    EXPECT_EQ(layout.attachment_count(), 2u);
    EXPECT_TRUE(layout.is_attached("surface.tip.left"));
    EXPECT_TRUE(layout.is_attached("surface.tip.center"));
}

// -----------------------------------------------------------------------------
// NFR-02：具名表達（無絕對像素座標於附著記錄本身）
// -----------------------------------------------------------------------------

TEST(Nfr02, AttachmentSpecCarriesNamedAnchorNotAbsoluteCoordinates) {
    SubordinateLayout layout;
    const AnchorSpec spec{Anchor::TopRight, Offset{-0.02f, 0.02f}};
    ASSERT_EQ(layout.attach("surface.badge", "surface.icon", spec), AnchorStatus::Ok);

    // 同一具名附著套用到兩個不同尺寸 / 位置的父矩形，皆解析出「相對父」一致比例的結果——
    // 證明定位以具名 anchor + 相對偏移表達，不依賴任何硬編的絕對像素座標。
    ResolvedPlacement out_small;
    ResolvedPlacement out_large;
    ASSERT_EQ(layout.resolve_child("surface.badge", ResolvedPlacement{0.0f, 0.0f, 40.0f, 40.0f},
                                   Size{8.0f, 8.0f}, out_small),
              AnchorStatus::Ok);
    ASSERT_EQ(layout.resolve_child("surface.badge",
                                   ResolvedPlacement{1000.0f, 1000.0f, 400.0f, 400.0f},
                                   Size{80.0f, 80.0f}, out_large),
              AnchorStatus::Ok);
    // 兩者的「距父右緣間距 / 父寬」比例一致（相對定位的本質）。
    const float gap_small = (40.0f - (out_small.x + 8.0f - 0.0f)) / 40.0f;
    const float gap_large = (400.0f - (out_large.x + 80.0f - 1000.0f)) / 400.0f;
    EXPECT_NEAR(gap_small, gap_large, kEps);
}

}  // namespace
