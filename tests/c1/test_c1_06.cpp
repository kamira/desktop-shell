// tests/c1/test_c1_06.cpp — C1-06 Dock profile（gtest）
//
// 涵蓋：dock 組裝（建構預設）、dock_to_edge（成功 / 重複固定拒絕 / 無效邊 / 無效厚度 /
// 能力不存在拒絕）、E1-16 熱區叫出（probe_hot_zone 命中已固定邊緣時自動 reveal、未命中 /
// 非本 dock 邊緣 / 未啟用自動隱藏時皆為 no-op）、auto_hide / reveal / hide 行為、E1-01
// 頂層指派（layer_stack 驗證 Topmost + undock 後移除）、E1-02 輸入策略透傳
// （Interactive / ClickThrough 對映 InputPolicy / InputHitResult）、add_item（成功 / 重複 id
// 拒絕）、以及解構安全（開啟中解構不留殘留頂層指派）。
#include "dock_profile.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

using ds::kernel::CapabilityDecl;
using ds::kernel::CapabilityMatrix;
using ds::kernel::InputHitResult;
using ds::kernel::InputPolicy;
using ds::kernel::InputStrategy;
using ds::kernel::LayerStack;
using ds::kernel::SurfaceLayer;
using ds::profiles::AddItemResult;
using ds::profiles::DockEdge;
using ds::profiles::DockItem;
using ds::profiles::DockPoint;
using ds::profiles::DockProfile;
using ds::profiles::DockScreenExtent;
using ds::profiles::DockToEdgeResult;
using ds::profiles::DockVisibility;
using ds::profiles::RevealResult;

namespace {

// -----------------------------------------------------------------------------
// dock 組裝：建構預設值
// -----------------------------------------------------------------------------

TEST(DockProfileTest, ConstructedUndockedWithDefaults) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);

    EXPECT_EQ(dock.id(), "dock.main");
    EXPECT_FALSE(dock.is_docked());
    EXPECT_FALSE(dock.docked_edge().has_value());
    EXPECT_FALSE(dock.auto_hide());
    EXPECT_EQ(dock.visibility(), DockVisibility::Visible);
    EXPECT_EQ(dock.strategy(), InputStrategy::Interactive);
    EXPECT_TRUE(dock.items().empty());
}

TEST(DockProfileTest, ConstructedWithExplicitStrategy) {
    LayerStack layer_stack;
    DockProfile dock("dock.hint", layer_stack, InputStrategy::ClickThrough);
    EXPECT_EQ(dock.strategy(), InputStrategy::ClickThrough);
}

// -----------------------------------------------------------------------------
// dock_to_edge() —— E1-16 + E1-01 組裝
// -----------------------------------------------------------------------------

TEST(DockProfileTest, DockToEdgeSucceedsAndAssignsTopLayer) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);

    EXPECT_EQ(dock.dock_to_edge(DockEdge::Bottom, 0.1f), DockToEdgeResult::Ok);
    EXPECT_TRUE(dock.is_docked());
    ASSERT_TRUE(dock.docked_edge().has_value());
    EXPECT_EQ(*dock.docked_edge(), DockEdge::Bottom);
    EXPECT_EQ(dock.visibility(), DockVisibility::Visible);

    // --- E1-01 頂層：驗證 layer_stack 上的實際指派 ---
    ASSERT_TRUE(layer_stack.contains("dock.main"));
    const SurfaceLayer* layer = layer_stack.layer_of("dock.main");
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(*layer, SurfaceLayer::Topmost);
    EXPECT_EQ(layer_stack.topmost(), "dock.main");
}

TEST(DockProfileTest, DockToEdgeWhileAlreadyDockedFails) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.dock_to_edge(DockEdge::Left, 0.05f), DockToEdgeResult::Ok);

    EXPECT_EQ(dock.dock_to_edge(DockEdge::Right, 0.05f), DockToEdgeResult::AlreadyDocked);
    // 不靜默重新固定：仍固定於原邊緣。
    ASSERT_TRUE(dock.docked_edge().has_value());
    EXPECT_EQ(*dock.docked_edge(), DockEdge::Left);
}

TEST(DockProfileTest, DockToEdgeWithInvalidEdgeFails) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);

    const auto invalid_edge = static_cast<DockEdge>(99);
    EXPECT_EQ(dock.dock_to_edge(invalid_edge, 0.1f), DockToEdgeResult::InvalidEdge);
    EXPECT_FALSE(dock.is_docked());
    EXPECT_FALSE(layer_stack.contains("dock.main"));  // 未固定，頂層未被指派。
}

TEST(DockProfileTest, DockToEdgeWithInvalidThicknessFails) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);

    EXPECT_EQ(dock.dock_to_edge(DockEdge::Top, 0.0f), DockToEdgeResult::InvalidThickness);
    EXPECT_EQ(dock.dock_to_edge(DockEdge::Top, 1.5f), DockToEdgeResult::InvalidThickness);
    EXPECT_FALSE(dock.is_docked());
}

TEST(DockProfileTest, DockToEdgeRejectedWhenLayerCapabilityUnavailable) {
    LayerStack layer_stack(CapabilityMatrix(std::vector<CapabilityDecl>{}));  // 空矩陣：kernel.surface 未宣告。
    DockProfile dock("dock.main", layer_stack);

    EXPECT_EQ(dock.dock_to_edge(DockEdge::Bottom, 0.1f), DockToEdgeResult::RejectedNoCapability);
    EXPECT_FALSE(dock.is_docked());
    EXPECT_FALSE(layer_stack.contains("dock.main"));
}

TEST(DockProfileTest, UndockRemovesTopLayerAssignmentAndAllowsRedock) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.dock_to_edge(DockEdge::Left, 0.1f), DockToEdgeResult::Ok);

    EXPECT_TRUE(dock.undock());
    EXPECT_FALSE(dock.is_docked());
    EXPECT_FALSE(dock.docked_edge().has_value());
    EXPECT_FALSE(layer_stack.contains("dock.main"));

    // 解除後可重新固定至不同邊緣。
    EXPECT_EQ(dock.dock_to_edge(DockEdge::Right, 0.1f), DockToEdgeResult::Ok);
    ASSERT_TRUE(dock.docked_edge().has_value());
    EXPECT_EQ(*dock.docked_edge(), DockEdge::Right);
}

TEST(DockProfileTest, UndockWhenNotDockedFails) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    EXPECT_FALSE(dock.undock());  // no-op，不靜默。
}

// -----------------------------------------------------------------------------
// auto_hide / reveal / hide
// -----------------------------------------------------------------------------

TEST(DockProfileTest, SetAutoHideWhileUndockedFails) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    EXPECT_FALSE(dock.set_auto_hide(true));
    EXPECT_FALSE(dock.auto_hide());
}

TEST(DockProfileTest, SetAutoHideTrueHidesAndFiresOnHide) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.dock_to_edge(DockEdge::Bottom, 0.1f), DockToEdgeResult::Ok);

    bool hide_fired = false;
    dock.on_hide([&] { hide_fired = true; });

    EXPECT_TRUE(dock.set_auto_hide(true));
    EXPECT_TRUE(dock.auto_hide());
    EXPECT_EQ(dock.visibility(), DockVisibility::Hidden);
    EXPECT_TRUE(hide_fired);
}

TEST(DockProfileTest, SetAutoHideFalseRevealsAndFiresOnReveal) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.dock_to_edge(DockEdge::Bottom, 0.1f), DockToEdgeResult::Ok);
    ASSERT_TRUE(dock.set_auto_hide(true));
    ASSERT_EQ(dock.visibility(), DockVisibility::Hidden);

    bool reveal_fired = false;
    dock.on_reveal([&] { reveal_fired = true; });

    EXPECT_TRUE(dock.set_auto_hide(false));
    EXPECT_FALSE(dock.auto_hide());
    EXPECT_EQ(dock.visibility(), DockVisibility::Visible);
    EXPECT_TRUE(reveal_fired);
}

TEST(DockProfileTest, RevealWhileUndockedReturnsNotDocked) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    EXPECT_EQ(dock.reveal(), RevealResult::NotDocked);
}

TEST(DockProfileTest, RevealWhileAlreadyVisibleReturnsAlreadyVisible) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.dock_to_edge(DockEdge::Bottom, 0.1f), DockToEdgeResult::Ok);
    EXPECT_EQ(dock.visibility(), DockVisibility::Visible);
    EXPECT_EQ(dock.reveal(), RevealResult::AlreadyVisible);
}

TEST(DockProfileTest, RevealFromHiddenTransitionsToVisible) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.dock_to_edge(DockEdge::Bottom, 0.1f), DockToEdgeResult::Ok);
    ASSERT_TRUE(dock.set_auto_hide(true));
    ASSERT_EQ(dock.visibility(), DockVisibility::Hidden);

    EXPECT_EQ(dock.reveal(), RevealResult::Revealed);
    EXPECT_EQ(dock.visibility(), DockVisibility::Visible);
}

TEST(DockProfileTest, HideWhileUndockedFails) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    EXPECT_FALSE(dock.hide());
}

TEST(DockProfileTest, HideWhileAlreadyHiddenFails) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.dock_to_edge(DockEdge::Bottom, 0.1f), DockToEdgeResult::Ok);
    ASSERT_TRUE(dock.set_auto_hide(true));
    ASSERT_EQ(dock.visibility(), DockVisibility::Hidden);

    EXPECT_FALSE(dock.hide());  // 已隱藏，no-op，不靜默重複。
}

TEST(DockProfileTest, HideFromVisibleTransitionsToHiddenWithoutAutoHide) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.dock_to_edge(DockEdge::Bottom, 0.1f), DockToEdgeResult::Ok);
    EXPECT_FALSE(dock.auto_hide());  // 手動收合不要求已啟用自動隱藏。

    EXPECT_TRUE(dock.hide());
    EXPECT_EQ(dock.visibility(), DockVisibility::Hidden);
}

// -----------------------------------------------------------------------------
// E1-16 熱區叫出（probe_hot_zone）
// -----------------------------------------------------------------------------

TEST(DockProfileTest, ProbeHotZoneHitOnDockedEdgeRevealsWhileAutoHiding) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.dock_to_edge(DockEdge::Bottom, 0.1f), DockToEdgeResult::Ok);
    ASSERT_TRUE(dock.set_auto_hide(true));
    ASSERT_EQ(dock.visibility(), DockVisibility::Hidden);

    const DockScreenExtent screen{1000.0f, 800.0f};
    // 底部熱區 0.1 比例 -> y in [720, 800]；點 (500, 790) 落於其中。
    const DockPoint inside{500.0f, 790.0f};
    EXPECT_TRUE(dock.probe_hot_zone(inside, screen));
    EXPECT_EQ(dock.visibility(), DockVisibility::Visible);
}

TEST(DockProfileTest, ProbeHotZoneMissDoesNotReveal) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.dock_to_edge(DockEdge::Bottom, 0.1f), DockToEdgeResult::Ok);
    ASSERT_TRUE(dock.set_auto_hide(true));

    const DockScreenExtent screen{1000.0f, 800.0f};
    const DockPoint outside{500.0f, 100.0f};  // 遠離底部熱區。
    EXPECT_FALSE(dock.probe_hot_zone(outside, screen));
    EXPECT_EQ(dock.visibility(), DockVisibility::Hidden);
}

TEST(DockProfileTest, ProbeHotZoneWhileNotAutoHidingIsNoOp) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.dock_to_edge(DockEdge::Bottom, 0.1f), DockToEdgeResult::Ok);
    // 未啟用 auto_hide：始終可見，探測應為 no-op。

    const DockScreenExtent screen{1000.0f, 800.0f};
    const DockPoint inside{500.0f, 790.0f};
    EXPECT_FALSE(dock.probe_hot_zone(inside, screen));
    EXPECT_EQ(dock.visibility(), DockVisibility::Visible);
}

TEST(DockProfileTest, ProbeHotZoneWhileUndockedIsNoOp) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    const DockScreenExtent screen{1000.0f, 800.0f};
    const DockPoint point{500.0f, 790.0f};
    EXPECT_FALSE(dock.probe_hot_zone(point, screen));
}

// -----------------------------------------------------------------------------
// add_item
// -----------------------------------------------------------------------------

TEST(DockProfileTest, AddItemSucceedsAndFiresCallback) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);

    const DockItem* added = nullptr;
    dock.on_item_added([&](const DockItem& item) { added = &item; });

    EXPECT_EQ(dock.add_item("finder", "Finder"), AddItemResult::Ok);
    ASSERT_EQ(dock.items().size(), 1u);
    EXPECT_EQ(dock.items()[0].id, "finder");
    EXPECT_EQ(dock.items()[0].label, "Finder");
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->id, "finder");
}

TEST(DockProfileTest, AddItemDuplicateIdFails) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack);
    ASSERT_EQ(dock.add_item("finder", "Finder"), AddItemResult::Ok);

    EXPECT_EQ(dock.add_item("finder", "Finder Again"), AddItemResult::DuplicateId);
    ASSERT_EQ(dock.items().size(), 1u);            // 不靜默覆寫既有項目。
    EXPECT_EQ(dock.items()[0].label, "Finder");    // 原資料不動。
}

// -----------------------------------------------------------------------------
// E1-02 輸入策略整合
// -----------------------------------------------------------------------------

TEST(DockProfileTest, InteractiveStrategyMapsToAcceptingAndSolid) {
    LayerStack layer_stack;
    DockProfile dock("dock.main", layer_stack, InputStrategy::Interactive);
    EXPECT_EQ(dock.backend_input_policy(), InputPolicy::Accepting);
    EXPECT_EQ(dock.hit_result(), InputHitResult::Solid);
}

TEST(DockProfileTest, ClickThroughStrategyMapsToPassThroughAndTransparent) {
    LayerStack layer_stack;
    DockProfile dock("dock.hint", layer_stack, InputStrategy::ClickThrough);
    EXPECT_EQ(dock.backend_input_policy(), InputPolicy::PassThrough);
    EXPECT_EQ(dock.hit_result(), InputHitResult::Transparent);
}

// -----------------------------------------------------------------------------
// 解構安全
// -----------------------------------------------------------------------------

TEST(DockProfileTest, DestructorWhileDockedRemovesLayerAssignment) {
    LayerStack layer_stack;
    {
        DockProfile dock("dock.transient", layer_stack);
        ASSERT_EQ(dock.dock_to_edge(DockEdge::Left, 0.1f), DockToEdgeResult::Ok);
        ASSERT_TRUE(layer_stack.contains("dock.transient"));
    }  // 解構時仍固定；dtor 應強制 undock()，移除頂層指派。
    EXPECT_FALSE(layer_stack.contains("dock.transient"));
}

}  // namespace
