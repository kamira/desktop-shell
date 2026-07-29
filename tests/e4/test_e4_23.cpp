// E4-23 容器裁切與遮罩 — gtest 契約測試
//
// 涵蓋：矩形 / 圓角矩形 / path 裁切、alpha 遮罩、命中 / 可見判定、巢狀裁切、
// 無效裁切區域明確報錯（不靜默）、render_model 決定性快照。
#include "clip_mask.hpp"

#include <limits>
#include <vector>

#include <gtest/gtest.h>

using ds::render::ClipMaskService;
using ds::render::ClipRegion;
using ds::render::ClipShape;
using ds::render::ClipStatus;
using ds::render::MaskKind;
using ds::render::MaskSource;
using ds::render::NormPoint;
using ds::render::NormRect;

namespace {

ClipRegion make_rect(float x0, float y0, float x1, float y1) {
    ClipRegion r;
    r.shape = ClipShape::Rect;
    r.rect = NormRect{x0, y0, x1, y1};
    return r;
}

ClipRegion make_rounded(float x0, float y0, float x1, float y1, float radius) {
    ClipRegion r;
    r.shape = ClipShape::RoundedRect;
    r.rect = NormRect{x0, y0, x1, y1};
    r.corner_radius = radius;
    return r;
}

ClipRegion make_path(std::vector<NormPoint> pts) {
    ClipRegion r;
    r.shape = ClipShape::Path;
    r.path = std::move(pts);
    return r;
}

// --- 矩形裁切 ---
TEST(ClipMaskService, SetRectClipOkAndStored) {
    ClipMaskService svc;
    EXPECT_EQ(svc.set_clip("panel", make_rect(0.1f, 0.1f, 0.9f, 0.9f)), ClipStatus::Ok);
    EXPECT_TRUE(svc.has_clip("panel"));
    const ClipRegion* r = svc.clip_region("panel");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->shape, ClipShape::Rect);
    EXPECT_FLOAT_EQ(r->rect.x0, 0.1f);
}

TEST(ClipMaskService, RectClipInsideIsVisibleOutsideIsNot) {
    ClipMaskService svc;
    ASSERT_EQ(svc.set_clip("panel", make_rect(0.2f, 0.2f, 0.8f, 0.8f)), ClipStatus::Ok);
    EXPECT_TRUE(svc.is_visible("panel", NormPoint{0.5f, 0.5f}));
    EXPECT_TRUE(svc.is_visible("panel", NormPoint{0.2f, 0.2f}));  // 邊界含
    EXPECT_FALSE(svc.is_visible("panel", NormPoint{0.05f, 0.5f}));
    EXPECT_FALSE(svc.is_visible("panel", NormPoint{0.5f, 0.95f}));
}

TEST(ClipMaskService, NoClipMeansGloballyVisible) {
    ClipMaskService svc;
    EXPECT_FALSE(svc.has_clip("unset"));
    EXPECT_TRUE(svc.is_visible("unset", NormPoint{0.0f, 0.0f}));
    EXPECT_TRUE(svc.is_visible("unset", NormPoint{1.0f, 1.0f}));
}

TEST(ClipMaskService, ClearClipRestoresFullVisibility) {
    ClipMaskService svc;
    ASSERT_EQ(svc.set_clip("panel", make_rect(0.4f, 0.4f, 0.6f, 0.6f)), ClipStatus::Ok);
    EXPECT_FALSE(svc.is_visible("panel", NormPoint{0.0f, 0.0f}));
    EXPECT_EQ(svc.clear_clip("panel"), ClipStatus::Ok);
    EXPECT_FALSE(svc.has_clip("panel"));
    EXPECT_TRUE(svc.is_visible("panel", NormPoint{0.0f, 0.0f}));
}

// --- 圓角矩形裁切 ---
TEST(ClipMaskService, RoundedRectCenterAndEdgeMidpointsVisible) {
    ClipMaskService svc;
    ASSERT_EQ(svc.set_clip("card", make_rounded(0.0f, 0.0f, 1.0f, 1.0f, 0.2f)), ClipStatus::Ok);
    EXPECT_TRUE(svc.is_visible("card", NormPoint{0.5f, 0.5f}));   // 正中央
    EXPECT_TRUE(svc.is_visible("card", NormPoint{0.5f, 0.0f}));   // 頂邊中點（中央十字帶）
    EXPECT_TRUE(svc.is_visible("card", NormPoint{0.0f, 0.5f}));   // 左邊中點
}

TEST(ClipMaskService, RoundedRectCornerOutsideRadiusIsClipped) {
    ClipMaskService svc;
    // 半徑為短邊(=1.0)的 0.2 -> 絕對半徑 0.2。角落 (0,0) 距最近圓心 (0.2,0.2) 距離
    // = 0.2*sqrt(2) ≈ 0.283 > 0.2，應被裁掉。
    ASSERT_EQ(svc.set_clip("card", make_rounded(0.0f, 0.0f, 1.0f, 1.0f, 0.2f)), ClipStatus::Ok);
    EXPECT_FALSE(svc.is_visible("card", NormPoint{0.0f, 0.0f}));
}

TEST(ClipMaskService, RoundedRectCornerWithinRadiusIsVisible) {
    ClipMaskService svc;
    ASSERT_EQ(svc.set_clip("card", make_rounded(0.0f, 0.0f, 1.0f, 1.0f, 0.2f)), ClipStatus::Ok);
    // 圓心 (0.2, 0.2) 本身必落在圓內。
    EXPECT_TRUE(svc.is_visible("card", NormPoint{0.2f, 0.2f}));
}

TEST(ClipMaskService, ZeroRadiusRoundedRectBehavesAsPlainRect) {
    ClipMaskService svc;
    ASSERT_EQ(svc.set_clip("card", make_rounded(0.1f, 0.1f, 0.9f, 0.9f, 0.0f)), ClipStatus::Ok);
    EXPECT_TRUE(svc.is_visible("card", NormPoint{0.1f, 0.1f}));  // 角落也可見（無圓角）
}

// --- path 裁切 ---
TEST(ClipMaskService, TrianglePathClipsPointsOutsideTriangle) {
    ClipMaskService svc;
    // 三角形： (0.5,0.1) - (0.9,0.9) - (0.1,0.9)
    ClipRegion tri = make_path({NormPoint{0.5f, 0.1f}, NormPoint{0.9f, 0.9f}, NormPoint{0.1f, 0.9f}});
    ASSERT_EQ(svc.set_clip("badge", tri), ClipStatus::Ok);
    EXPECT_TRUE(svc.is_visible("badge", NormPoint{0.5f, 0.7f}));   // 三角形內部
    EXPECT_FALSE(svc.is_visible("badge", NormPoint{0.05f, 0.05f}));  // 三角形外（左上角）
    EXPECT_FALSE(svc.is_visible("badge", NormPoint{0.95f, 0.95f}));  // 三角形外（右下角）
}

// --- alpha 遮罩 ---
TEST(ClipMaskService, ApplyUniformMaskClampsCoverage) {
    ClipMaskService svc;
    MaskSource m;
    m.kind = MaskKind::Uniform;
    m.coverage = 1.5f;  // 超出範圍，應被 clamp
    EXPECT_EQ(svc.apply_mask("glow", m), ClipStatus::Ok);
    const MaskSource* stored = svc.mask_source("glow");
    ASSERT_NE(stored, nullptr);
    EXPECT_FLOAT_EQ(stored->coverage, 1.0f);
}

TEST(ClipMaskService, ZeroCoverageMaskHidesContainer) {
    ClipMaskService svc;
    MaskSource m;
    m.kind = MaskKind::Uniform;
    m.coverage = 0.0f;
    ASSERT_EQ(svc.apply_mask("ghost", m), ClipStatus::Ok);
    EXPECT_FALSE(svc.is_visible("ghost", NormPoint{0.5f, 0.5f}));
}

TEST(ClipMaskService, NamedPatternMaskRequiresPatternName) {
    ClipMaskService svc;
    MaskSource m;
    m.kind = MaskKind::NamedPattern;
    m.pattern_name = "";  // 空名字：無效
    EXPECT_EQ(svc.apply_mask("panel", m), ClipStatus::Invalid);

    m.pattern_name = "fade-edge";
    EXPECT_EQ(svc.apply_mask("panel", m), ClipStatus::Ok);
    const MaskSource* stored = svc.mask_source("panel");
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->pattern_name, "fade-edge");
}

TEST(ClipMaskService, ClearMaskRestoresFullVisibility) {
    ClipMaskService svc;
    MaskSource m;
    m.coverage = 0.0f;
    ASSERT_EQ(svc.apply_mask("panel", m), ClipStatus::Ok);
    EXPECT_FALSE(svc.is_visible("panel", NormPoint{0.5f, 0.5f}));
    EXPECT_EQ(svc.clear_mask("panel"), ClipStatus::Ok);
    EXPECT_FALSE(svc.has_mask("panel"));
    EXPECT_TRUE(svc.is_visible("panel", NormPoint{0.5f, 0.5f}));
}

// --- 命中 / 可見判定：越界點與非有限值一律不可見 ---
TEST(ClipMaskService, OutOfUnitRangePointIsNeverVisible) {
    ClipMaskService svc;  // 甚至無任何裁切設定
    EXPECT_FALSE(svc.is_visible("anything", NormPoint{-0.1f, 0.5f}));
    EXPECT_FALSE(svc.is_visible("anything", NormPoint{0.5f, 1.1f}));
}

// --- 巢狀裁切 ---
TEST(ClipMaskService, NestedClipIntersectsWithAncestor) {
    ClipMaskService svc;
    ASSERT_EQ(svc.set_clip("parent", make_rect(0.0f, 0.0f, 0.5f, 1.0f)), ClipStatus::Ok);
    ASSERT_EQ(svc.set_clip("child", make_rect(0.0f, 0.0f, 1.0f, 1.0f)), ClipStatus::Ok);
    ASSERT_EQ(svc.set_parent("child", "parent"), ClipStatus::Ok);
    EXPECT_EQ(svc.parent_of("child"), "parent");

    // 點在 child 自身裁切內，但超出 parent 裁切（x=0.7 > parent 的 x1=0.5）-> 不可見。
    EXPECT_FALSE(svc.is_visible("child", NormPoint{0.7f, 0.5f}));
    // 點同時落在 child 與 parent 裁切內 -> 可見。
    EXPECT_TRUE(svc.is_visible("child", NormPoint{0.3f, 0.5f}));
}

TEST(ClipMaskService, NestedClipWithoutOwnClipInheritsAncestorOnly) {
    ClipMaskService svc;
    ASSERT_EQ(svc.set_clip("parent", make_rect(0.25f, 0.25f, 0.75f, 0.75f)), ClipStatus::Ok);
    ASSERT_EQ(svc.set_parent("child", "parent"), ClipStatus::Ok);  // child 自身無裁切
    EXPECT_TRUE(svc.is_visible("child", NormPoint{0.5f, 0.5f}));
    EXPECT_FALSE(svc.is_visible("child", NormPoint{0.9f, 0.9f}));  // 超出 parent 裁切
}

TEST(ClipMaskService, SetParentSelfIsInvalid) {
    ClipMaskService svc;
    EXPECT_EQ(svc.set_parent("a", "a"), ClipStatus::Invalid);
}

TEST(ClipMaskService, SetParentCycleIsRejected) {
    ClipMaskService svc;
    ASSERT_EQ(svc.set_parent("b", "a"), ClipStatus::Ok);
    ASSERT_EQ(svc.set_parent("c", "b"), ClipStatus::Ok);
    // a -> c 會形成 a -> c -> b -> a 迴圈，應被拒絕。
    EXPECT_EQ(svc.set_parent("a", "c"), ClipStatus::Invalid);
    EXPECT_EQ(svc.parent_of("a"), "");  // 未被建立
}

TEST(ClipMaskService, ClearParentDetachesFromAncestorClip) {
    ClipMaskService svc;
    ASSERT_EQ(svc.set_clip("parent", make_rect(0.0f, 0.0f, 0.3f, 0.3f)), ClipStatus::Ok);
    ASSERT_EQ(svc.set_parent("child", "parent"), ClipStatus::Ok);
    EXPECT_FALSE(svc.is_visible("child", NormPoint{0.5f, 0.5f}));
    EXPECT_EQ(svc.clear_parent("child"), ClipStatus::Ok);
    EXPECT_EQ(svc.parent_of("child"), "");
    EXPECT_TRUE(svc.is_visible("child", NormPoint{0.5f, 0.5f}));
}

// --- 無效裁切區域：明確報錯，不靜默 ---
TEST(ClipMaskService, EmptyContainerIdIsInvalid) {
    ClipMaskService svc;
    EXPECT_EQ(svc.set_clip("", make_rect(0.0f, 0.0f, 1.0f, 1.0f)), ClipStatus::Invalid);
}

TEST(ClipMaskService, DegenerateRectIsInvalid) {
    ClipMaskService svc;
    // x0 >= x1：無效矩形。
    EXPECT_EQ(svc.set_clip("panel", make_rect(0.5f, 0.0f, 0.5f, 1.0f)), ClipStatus::Invalid);
    EXPECT_FALSE(svc.has_clip("panel"));  // 不留半份狀態
}

TEST(ClipMaskService, RectOutOfUnitRangeIsInvalid) {
    ClipMaskService svc;
    EXPECT_EQ(svc.set_clip("panel", make_rect(-0.1f, 0.0f, 0.5f, 1.0f)), ClipStatus::Invalid);
    EXPECT_EQ(svc.set_clip("panel", make_rect(0.0f, 0.0f, 1.5f, 1.0f)), ClipStatus::Invalid);
}

TEST(ClipMaskService, RoundedRectRadiusOutOfRangeIsInvalid) {
    ClipMaskService svc;
    EXPECT_EQ(svc.set_clip("card", make_rounded(0.0f, 0.0f, 1.0f, 1.0f, -0.1f)), ClipStatus::Invalid);
    EXPECT_EQ(svc.set_clip("card", make_rounded(0.0f, 0.0f, 1.0f, 1.0f, 0.6f)), ClipStatus::Invalid);
}

TEST(ClipMaskService, PathWithFewerThanThreePointsIsInvalid) {
    ClipMaskService svc;
    EXPECT_EQ(svc.set_clip("badge", make_path({NormPoint{0.1f, 0.1f}, NormPoint{0.5f, 0.5f}})),
              ClipStatus::Invalid);
}

TEST(ClipMaskService, PathWithOutOfRangePointIsInvalid) {
    ClipMaskService svc;
    ClipRegion bad =
        make_path({NormPoint{0.1f, 0.1f}, NormPoint{1.5f, 0.5f}, NormPoint{0.1f, 0.9f}});
    EXPECT_EQ(svc.set_clip("badge", bad), ClipStatus::Invalid);
}

TEST(ClipMaskService, NonFiniteValuesAreInvalid) {
    ClipMaskService svc;
    ClipRegion nan_rect = make_rect(0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f);
    EXPECT_EQ(svc.set_clip("panel", nan_rect), ClipStatus::Invalid);

    MaskSource m;
    m.coverage = std::numeric_limits<float>::infinity();
    EXPECT_EQ(svc.apply_mask("panel", m), ClipStatus::Invalid);
}

// --- render_model：決定性宣告式快照 ---
TEST(ClipMaskService, RenderModelReflectsClipMaskAndParent) {
    ClipMaskService svc;
    ASSERT_EQ(svc.set_clip("b_panel", make_rect(0.0f, 0.0f, 0.5f, 0.5f)), ClipStatus::Ok);
    ASSERT_EQ(svc.set_clip("a_panel", make_rect(0.1f, 0.1f, 0.9f, 0.9f)), ClipStatus::Ok);
    MaskSource m;
    m.coverage = 0.5f;
    ASSERT_EQ(svc.apply_mask("a_panel", m), ClipStatus::Ok);
    ASSERT_EQ(svc.set_parent("b_panel", "a_panel"), ClipStatus::Ok);

    auto model = svc.render_model();
    ASSERT_EQ(model.size(), 2u);
    // 依具名鍵字典序：a_panel 先於 b_panel。
    EXPECT_EQ(model[0].container, "a_panel");
    EXPECT_TRUE(model[0].has_clip);
    EXPECT_TRUE(model[0].has_mask);
    EXPECT_FLOAT_EQ(model[0].mask.coverage, 0.5f);
    EXPECT_EQ(model[0].parent, "");

    EXPECT_EQ(model[1].container, "b_panel");
    EXPECT_TRUE(model[1].has_clip);
    EXPECT_FALSE(model[1].has_mask);
    EXPECT_EQ(model[1].parent, "a_panel");
}

TEST(ClipMaskService, RenderModelEmptyWhenNoState) {
    ClipMaskService svc;
    EXPECT_TRUE(svc.render_model().empty());
}

TEST(ClipMaskService, RenderModelIncludesParentOnlyEntry) {
    ClipMaskService svc;
    ASSERT_EQ(svc.set_parent("child", "parent"), ClipStatus::Ok);
    auto model = svc.render_model();
    // "child" 與 "parent" 兩者皆出現（parent 僅因被引用為父代亦收入快照）。
    ASSERT_EQ(model.size(), 1u);
    EXPECT_EQ(model[0].container, "child");
    EXPECT_FALSE(model[0].has_clip);
    EXPECT_FALSE(model[0].has_mask);
    EXPECT_EQ(model[0].parent, "parent");
}

}  // namespace
