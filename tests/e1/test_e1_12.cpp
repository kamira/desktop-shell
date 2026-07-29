// E1-12 多重從屬 surface 並存 — 單元測試（gtest）
//
// 驗證一個父 surface 可同時掛多個從屬(子) surface，各自以 E1-11 定位、獨立管理、
// 父移動時全部跟隨、可個別 / 全部關閉、避免互相衝突：
//   - 多子並存：同一 parent 下多個 child 各自附著、各自定位(委由 E1-11 resolve_child)
//   - children_of：列舉某 parent 目前所有直接子(依附著順序)
//   - reposition_all：父移動 / 改變尺寸後，一次性批次重新解析其下所有子的絕對佈局
//   - detach_child：個別關閉單一子，不影響同 parent 下其餘子
//   - detach_all：全部關閉 parent 底下所有子(含巢狀子孫)，委由 E1-11 close_parent
//   - 多父多子：不同 parent 各自管理各自的子，互不干擾
//   - 無效附著：空 id / 自附 / 循環 / 無效 spec / 非有限元件尺寸 → Invalid，不記錄
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實視窗 / 繪圖 API。
#include "multi_subordinate_manager.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

using ds::kernel::Anchor;
using ds::kernel::AnchorSpec;
using ds::kernel::AnchorStatus;
using ds::kernel::MultiSubordinateManager;
using ds::kernel::Offset;
using ds::kernel::ResolvedPlacement;
using ds::kernel::Size;
using ds::kernel::SurfaceId;

namespace {

constexpr float kEps = 1e-5f;

bool contains(const std::vector<SurfaceId>& v, const SurfaceId& id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

// -----------------------------------------------------------------------------
// 多子並存：同一 parent 下多個 child 各自附著、各自定位
// -----------------------------------------------------------------------------

TEST(MultiSubordinate, MultipleChildrenCoexistUnderSameParent) {
    MultiSubordinateManager mgr;
    EXPECT_FALSE(mgr.has_children("surface.toolbar"));
    EXPECT_EQ(mgr.child_count("surface.toolbar"), 0u);

    EXPECT_EQ(mgr.attach_child("surface.toolbar", "surface.tooltip", {Anchor::BottomLeft, {}},
                               Size{20.0f, 10.0f}),
              AnchorStatus::Ok);
    EXPECT_EQ(mgr.attach_child("surface.toolbar", "surface.dropdown", {Anchor::BottomRight, {}},
                               Size{40.0f, 30.0f}),
              AnchorStatus::Ok);
    EXPECT_EQ(mgr.attach_child("surface.toolbar", "surface.annotation", {Anchor::BottomCenter, {}},
                               Size{10.0f, 10.0f}),
              AnchorStatus::Ok);

    EXPECT_TRUE(mgr.has_children("surface.toolbar"));
    EXPECT_EQ(mgr.child_count("surface.toolbar"), 3u);
    EXPECT_TRUE(mgr.is_attached("surface.tooltip"));
    EXPECT_TRUE(mgr.is_attached("surface.dropdown"));
    EXPECT_TRUE(mgr.is_attached("surface.annotation"));
    EXPECT_EQ(mgr.attachment_count(), 3u);
}

TEST(MultiSubordinate, EachChildResolvesIndependentlyViaE1_11) {
    MultiSubordinateManager mgr;
    ASSERT_EQ(mgr.attach_child("surface.toolbar", "surface.tip.left", {Anchor::BottomLeft, {}},
                               Size{20.0f, 10.0f}),
              AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.toolbar", "surface.tip.right", {Anchor::BottomRight, {}},
                               Size{20.0f, 10.0f}),
              AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.toolbar", "surface.tip.center", {Anchor::BottomCenter, {}},
                               Size{20.0f, 10.0f}),
              AnchorStatus::Ok);

    const ResolvedPlacement parent{0.0f, 0.0f, 300.0f, 30.0f};
    ResolvedPlacement left;
    ResolvedPlacement right;
    ResolvedPlacement center;
    // 直接透過 reposition_all 的結果驗證，也可用個別呼叫(此處驗證單子解析路徑一致)。
    const auto all = mgr.reposition_all("surface.toolbar", parent);
    ASSERT_EQ(all.size(), 3u);
    for (const auto& entry : all) {
        if (entry.first == "surface.tip.left") left = entry.second;
        if (entry.first == "surface.tip.right") right = entry.second;
        if (entry.first == "surface.tip.center") center = entry.second;
    }

    EXPECT_NEAR(left.x, 0.0f, kEps);
    EXPECT_NEAR(right.x, 280.0f, kEps);   // 300 - 20
    EXPECT_NEAR(center.x, 140.0f, kEps);  // (300-20)/2
    // 各自獨立，互不覆寫。
    EXPECT_NE(left.x, right.x);
    EXPECT_NE(left.x, center.x);
}

// -----------------------------------------------------------------------------
// children_of：列舉 parent 目前所有直接子
// -----------------------------------------------------------------------------

TEST(ChildrenOf, EnumeratesDirectChildrenInAttachOrder) {
    MultiSubordinateManager mgr;
    ASSERT_EQ(mgr.attach_child("surface.panel", "surface.a", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.panel", "surface.b", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.panel", "surface.c", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);

    const auto kids = mgr.children_of("surface.panel");
    ASSERT_EQ(kids.size(), 3u);
    EXPECT_EQ(kids[0], "surface.a");
    EXPECT_EQ(kids[1], "surface.b");
    EXPECT_EQ(kids[2], "surface.c");
}

TEST(ChildrenOf, UnknownOrChildlessParentReturnsEmpty) {
    MultiSubordinateManager mgr;
    EXPECT_TRUE(mgr.children_of("surface.nothing").empty());

    // 已存在的 parent 但目前無任何子(尚未附著) → 依然空。
    ASSERT_EQ(mgr.attach_child("surface.host", "surface.tip", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);
    EXPECT_TRUE(mgr.children_of("surface.tip").empty());  // tip 是 child 不是任何 parent
}

// -----------------------------------------------------------------------------
// reposition_all：父移動 / 改變尺寸後，批次重新解析所有子(父移動時全部跟隨)
// -----------------------------------------------------------------------------

TEST(RepositionAll, AllChildrenFollowWhenParentRectChanges) {
    MultiSubordinateManager mgr;
    ASSERT_EQ(mgr.attach_child("surface.button", "surface.tip1", {Anchor::TopCenter, {}},
                               Size{20.0f, 10.0f}),
              AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.button", "surface.tip2", {Anchor::BottomCenter, {}},
                               Size{20.0f, 10.0f}),
              AnchorStatus::Ok);

    const auto before = mgr.reposition_all("surface.button", ResolvedPlacement{0.0f, 0.0f, 60.0f, 30.0f});
    ASSERT_EQ(before.size(), 2u);

    const auto after =
        mgr.reposition_all("surface.button", ResolvedPlacement{300.0f, 500.0f, 60.0f, 30.0f});
    ASSERT_EQ(after.size(), 2u);

    for (std::size_t i = 0; i < before.size(); ++i) {
        EXPECT_EQ(before[i].first, after[i].first);
        // 同尺寸、原點平移 (+300, +500)：每個子都整體平移相同量(全部跟隨)。
        EXPECT_NEAR(after[i].second.x, before[i].second.x + 300.0f, kEps);
        EXPECT_NEAR(after[i].second.y, before[i].second.y + 500.0f, kEps);
    }
}

TEST(RepositionAll, ChildlessParentReturnsEmpty) {
    MultiSubordinateManager mgr;
    const auto result = mgr.reposition_all("surface.nobody", ResolvedPlacement{0, 0, 10, 10});
    EXPECT_TRUE(result.empty());
}

// -----------------------------------------------------------------------------
// detach_child：個別關閉，不影響同 parent 下其餘子
// -----------------------------------------------------------------------------

TEST(DetachChild, RemovesOnlyTargetChildKeepsSiblings) {
    MultiSubordinateManager mgr;
    ASSERT_EQ(mgr.attach_child("surface.panel", "surface.a", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.panel", "surface.b", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);

    EXPECT_EQ(mgr.detach_child("surface.a"), AnchorStatus::Ok);
    EXPECT_FALSE(mgr.is_attached("surface.a"));
    EXPECT_TRUE(mgr.is_attached("surface.b"));
    EXPECT_EQ(mgr.child_count("surface.panel"), 1u);
    EXPECT_FALSE(contains(mgr.children_of("surface.panel"), "surface.a"));
    EXPECT_TRUE(contains(mgr.children_of("surface.panel"), "surface.b"));

    // 元件尺寸記錄也一併清除(不留孤兒)。
    EXPECT_EQ(mgr.element_size_of("surface.a"), nullptr);
    EXPECT_NE(mgr.element_size_of("surface.b"), nullptr);
}

TEST(DetachChild, UnknownChildIsInvalidNotCrash) {
    MultiSubordinateManager mgr;
    EXPECT_EQ(mgr.detach_child("surface.ghost"), AnchorStatus::Invalid);
}

// -----------------------------------------------------------------------------
// detach_all：全部關閉，含巢狀子孫
// -----------------------------------------------------------------------------

TEST(DetachAll, RemovesAllDirectChildren) {
    MultiSubordinateManager mgr;
    ASSERT_EQ(mgr.attach_child("surface.panel", "surface.tip1", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.panel", "surface.tip2", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.panel", "surface.tip3", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);
    EXPECT_EQ(mgr.child_count("surface.panel"), 3u);

    const std::size_t removed = mgr.detach_all("surface.panel");
    EXPECT_EQ(removed, 3u);
    EXPECT_FALSE(mgr.has_children("surface.panel"));
    EXPECT_EQ(mgr.child_count("surface.panel"), 0u);
    EXPECT_FALSE(mgr.is_attached("surface.tip1"));
    EXPECT_FALSE(mgr.is_attached("surface.tip2"));
    EXPECT_FALSE(mgr.is_attached("surface.tip3"));
    // 元件尺寸記錄一併清光。
    EXPECT_EQ(mgr.element_size_of("surface.tip1"), nullptr);
    EXPECT_EQ(mgr.element_size_of("surface.tip2"), nullptr);
    EXPECT_EQ(mgr.element_size_of("surface.tip3"), nullptr);
}

TEST(DetachAll, CascadesNestedSubordinateChainAndSelf) {
    MultiSubordinateManager mgr;
    // panel -> dropdown -> submenu（巢狀從屬鏈）+ 一個與此鏈無關的附著。
    ASSERT_EQ(mgr.attach_child("surface.panel", "surface.dropdown", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);
    ASSERT_EQ(
        mgr.attach_child("surface.dropdown", "surface.submenu", {Anchor::Center, {}}, Size{1, 1}),
        AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.other", "surface.unrelated", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);
    EXPECT_EQ(mgr.attachment_count(), 3u);

    // 關閉 dropdown：連帶清除 dropdown 自身 + submenu，不影響 unrelated。
    const std::size_t removed = mgr.detach_all("surface.dropdown");
    EXPECT_EQ(removed, 2u);
    EXPECT_FALSE(mgr.is_attached("surface.dropdown"));
    EXPECT_FALSE(mgr.is_attached("surface.submenu"));
    EXPECT_TRUE(mgr.is_attached("surface.unrelated"));
    EXPECT_EQ(mgr.attachment_count(), 1u);
    EXPECT_EQ(mgr.element_size_of("surface.dropdown"), nullptr);
    EXPECT_EQ(mgr.element_size_of("surface.submenu"), nullptr);
    EXPECT_NE(mgr.element_size_of("surface.unrelated"), nullptr);
}

TEST(DetachAll, ParentWithNoAttachmentsRemovesZero) {
    MultiSubordinateManager mgr;
    EXPECT_EQ(mgr.detach_all("surface.nothing"), 0u);
}

// -----------------------------------------------------------------------------
// 多父多子：不同 parent 各自管理各自的子，互不干擾
// -----------------------------------------------------------------------------

TEST(MultiParent, EachParentManagesItsOwnChildrenIndependently) {
    MultiSubordinateManager mgr;
    ASSERT_EQ(mgr.attach_child("surface.window.a", "surface.tip.a1", {Anchor::Center, {}},
                               Size{10, 10}),
              AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.window.a", "surface.tip.a2", {Anchor::Center, {}},
                               Size{10, 10}),
              AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.window.b", "surface.tip.b1", {Anchor::Center, {}},
                               Size{10, 10}),
              AnchorStatus::Ok);

    EXPECT_EQ(mgr.child_count("surface.window.a"), 2u);
    EXPECT_EQ(mgr.child_count("surface.window.b"), 1u);
    EXPECT_TRUE(contains(mgr.children_of("surface.window.a"), "surface.tip.a1"));
    EXPECT_TRUE(contains(mgr.children_of("surface.window.a"), "surface.tip.a2"));
    EXPECT_FALSE(contains(mgr.children_of("surface.window.a"), "surface.tip.b1"));
    EXPECT_TRUE(contains(mgr.children_of("surface.window.b"), "surface.tip.b1"));

    // 移動/重解析 window.a 不影響 window.b 的子的附著狀態。
    const auto resolved_a =
        mgr.reposition_all("surface.window.a", ResolvedPlacement{0, 0, 100, 100});
    EXPECT_EQ(resolved_a.size(), 2u);
    EXPECT_TRUE(mgr.is_attached("surface.tip.b1"));

    // 全部關閉 window.a 不影響 window.b。
    EXPECT_EQ(mgr.detach_all("surface.window.a"), 2u);
    EXPECT_EQ(mgr.child_count("surface.window.a"), 0u);
    EXPECT_EQ(mgr.child_count("surface.window.b"), 1u);
    EXPECT_TRUE(mgr.is_attached("surface.tip.b1"));
}

// -----------------------------------------------------------------------------
// 無效附著：空 id / 自附 / 循環 / 無效 spec / 非有限元件尺寸
// -----------------------------------------------------------------------------

TEST(InvalidAttachment, EmptyIdsRejected) {
    MultiSubordinateManager mgr;
    EXPECT_EQ(mgr.attach_child("", "surface.child", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Invalid);
    EXPECT_EQ(mgr.attach_child("surface.parent", "", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Invalid);
    EXPECT_EQ(mgr.attachment_count(), 0u);
}

TEST(InvalidAttachment, SelfAttachRejected) {
    MultiSubordinateManager mgr;
    EXPECT_EQ(mgr.attach_child("surface.x", "surface.x", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Invalid);
    EXPECT_FALSE(mgr.is_attached("surface.x"));
}

TEST(InvalidAttachment, CycleRejected) {
    MultiSubordinateManager mgr;
    // surface.a 附著到 surface.b（a 的 parent 是 b）。
    ASSERT_EQ(mgr.attach_child("surface.b", "surface.a", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Ok);
    // 再嘗試讓 surface.b 附著到 surface.a：a 的 parent 已是 b，若放行會形成 a<->b 循環：拒絕，
    // 且不變更既有記錄（surface.b 仍未被附著，attachment_count 仍是 1）。
    EXPECT_EQ(mgr.attach_child("surface.a", "surface.b", {Anchor::Center, {}}, Size{1, 1}),
              AnchorStatus::Invalid);
    EXPECT_TRUE(mgr.is_attached("surface.a"));   // 既有附著不受影響
    EXPECT_FALSE(mgr.is_attached("surface.b"));  // 循環附著未被記錄
    EXPECT_EQ(mgr.attachment_count(), 1u);
}

TEST(InvalidAttachment, InvalidAnchorSpecRejected) {
    MultiSubordinateManager mgr;
    AnchorSpec bad_anchor;
    bad_anchor.anchor = static_cast<Anchor>(42);
    EXPECT_EQ(mgr.attach_child("surface.parent", "surface.child", bad_anchor, Size{1, 1}),
              AnchorStatus::Invalid);

    const AnchorSpec bad_offset{Anchor::Center,
                                Offset{std::numeric_limits<float>::infinity(), 0.0f}};
    EXPECT_EQ(mgr.attach_child("surface.parent", "surface.child2", bad_offset, Size{1, 1}),
              AnchorStatus::Invalid);
    EXPECT_EQ(mgr.attachment_count(), 0u);
}

TEST(InvalidAttachment, NonFiniteOrNegativeElementSizeRejected) {
    MultiSubordinateManager mgr;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(mgr.attach_child("surface.parent", "surface.child", {Anchor::Center, {}},
                               Size{nan, 1.0f}),
              AnchorStatus::Invalid);
    EXPECT_EQ(mgr.attach_child("surface.parent", "surface.child", {Anchor::Center, {}},
                               Size{-1.0f, 1.0f}),
              AnchorStatus::Invalid);
    EXPECT_FALSE(mgr.is_attached("surface.child"));
    EXPECT_EQ(mgr.element_size_of("surface.child"), nullptr);
}

TEST(InvalidAttachment, ReattachUpdatesInPlaceNotSecondRecord) {
    MultiSubordinateManager mgr;
    ASSERT_EQ(mgr.attach_child("surface.panel", "surface.menu", {Anchor::TopLeft, {}},
                               Size{10.0f, 10.0f}),
              AnchorStatus::Ok);
    ASSERT_EQ(mgr.attach_child("surface.other", "surface.menu", {Anchor::BottomRight, {}},
                               Size{20.0f, 20.0f}),
              AnchorStatus::Ok);
    EXPECT_EQ(mgr.attachment_count(), 1u);  // 就地更新，不新增第二筆
    ASSERT_NE(mgr.parent_of("surface.menu"), nullptr);
    EXPECT_EQ(*mgr.parent_of("surface.menu"), "surface.other");
    EXPECT_EQ(mgr.child_count("surface.panel"), 0u);  // 舊 parent 底下已不再有此子
    EXPECT_EQ(mgr.child_count("surface.other"), 1u);
    ASSERT_NE(mgr.element_size_of("surface.menu"), nullptr);
    EXPECT_NEAR(mgr.element_size_of("surface.menu")->width, 20.0f, kEps);
}

}  // namespace
