// E1-06 命中與繪製層序 — 單元測試（gtest）
//
// 驗證 PaintHitOrder 於相位 1（Mac / null 期）「命中層序與繪製層序一致」的核心保證：
//   - 繪製序 -> 命中反序：陣列順序即繪製順序（先繪製在前），命中依反序判定
//   - 最上層先命中：最後繪製（視覺最上層）者最先被檢查
//   - 重疊上層勝：多個 surface 重疊同一點時，繪製序上較後者（視覺較上層）勝出
//   - 具名層序：具名 `SurfaceLayer` 僅為個別 surface 屬性，不覆蓋繪製順序本身的判定依據
//   - 與 E1-04 一致：當繪製順序恰為「具名圖層 + 宣告順序」時，結果與 E1-04
//     `HitTester::topmost_hit` 一致（兩者對同一組 surface、同一點給出相同命中者）
//   - 順序變更後命中隨之變：`set_paint_order` 改變順序後，同一點的命中結果隨之改變
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實視窗系統 / 繪圖 API。
#include "paint_hit_order.hpp"

#include <gtest/gtest.h>

#include <vector>

using ds::kernel::AlphaMode;
using ds::kernel::HitPolicy;
using ds::kernel::HitStatus;
using ds::kernel::HitSurface;
using ds::kernel::HitTester;
using ds::kernel::LocalPoint;
using ds::kernel::make_rect;
using ds::kernel::PaintHitOrder;
using ds::kernel::Shape;
using ds::kernel::SurfaceLayer;

namespace {

// 便利工廠：一個覆蓋 [0,0]-[10,10] 的實心不透明矩形 surface，指定具名 id + 具名圖層。
HitSurface make_solid_surface(const std::string& id,
                              SurfaceLayer layer = SurfaceLayer::Normal) {
    HitSurface s;
    s.id = id;
    s.shape = make_rect(10.0f, 10.0f);
    s.layer = layer;
    s.hit = HitPolicy::Solid;
    s.alpha.mode = AlphaMode::Opaque;
    return s;
}

const LocalPoint kCenter{5.0f, 5.0f};  // 落在所有測試用矩形內的點

}  // namespace

// -----------------------------------------------------------------------------
// 繪製序 -> 命中反序 / 最上層先命中
// -----------------------------------------------------------------------------

TEST(PaintOrderToHitOrder, ReverseOfPaintOrderDeterminesHit) {
    PaintHitOrder order;
    // 繪製序：a 最先繪製（最底層）-> b -> c 最後繪製（視覺最上層）。
    order.set_paint_order({
        make_solid_surface("surface.a"),
        make_solid_surface("surface.b"),
        make_solid_surface("surface.c"),
    });

    const auto r = order.hit_topmost(kCenter);
    EXPECT_EQ(r.status, HitStatus::Ok);
    ASSERT_TRUE(r.hit);
    // 三者於該點皆重疊；命中反序（最後繪製先檢查）保證視覺最上層 c 勝出。
    EXPECT_EQ(r.id, "surface.c");
}

TEST(PaintOrderToHitOrder, TopmostPaintedSurfaceHitsFirstEvenWhenListedLast) {
    PaintHitOrder order;
    // 只有最後繪製的 "top" 覆蓋該點；驗證確實是「最後繪製者」被視為最上層並命中。
    order.set_paint_order({
        make_solid_surface("bottom"),
        make_solid_surface("middle"),
        make_solid_surface("top"),
    });

    const auto r = order.hit_topmost(kCenter);
    ASSERT_TRUE(r.hit);
    EXPECT_EQ(r.id, "top");
}

// -----------------------------------------------------------------------------
// 重疊上層勝
// -----------------------------------------------------------------------------

TEST(OverlapResolution, OverlappingSurfacesTopOfPaintOrderWins) {
    PaintHitOrder order;
    // back 完全覆蓋較大範圍；front 較小但重疊於 kCenter，且繪製在 back 之後（視覺更上層）。
    HitSurface back = make_solid_surface("back");
    back.shape = make_rect(20.0f, 20.0f);
    HitSurface front = make_solid_surface("front");
    front.shape = make_rect(10.0f, 10.0f);

    order.set_paint_order({back, front});

    const auto at_overlap = order.hit_topmost(kCenter);
    ASSERT_TRUE(at_overlap.hit);
    EXPECT_EQ(at_overlap.id, "front");  // 重疊區：繪製上較後（較上層）者勝

    // 只被 back 覆蓋、不被 front 覆蓋的區域：back 命中（驗證非「永遠回傳最後一個」）。
    const auto only_back = order.hit_topmost(LocalPoint{15.0f, 15.0f});
    ASSERT_TRUE(only_back.hit);
    EXPECT_EQ(only_back.id, "back");
}

TEST(OverlapResolution, NoHitWhenPointOutsideAllSurfaces) {
    PaintHitOrder order;
    order.set_paint_order({make_solid_surface("only")});

    const auto r = order.hit_topmost(LocalPoint{100.0f, 100.0f});
    EXPECT_EQ(r.status, HitStatus::Ok);
    EXPECT_FALSE(r.hit);
    EXPECT_TRUE(r.id.empty());
}

// -----------------------------------------------------------------------------
// 具名層序：SurfaceLayer 為個別 surface 屬性，不覆蓋繪製順序本身的判定依據
// -----------------------------------------------------------------------------

TEST(NamedLayerInteraction, PaintOrderRulesEvenWhenNamedLayerRankDisagrees) {
    PaintHitOrder order;
    // 刻意讓具名圖層 rank 與繪製順序相反：先繪製者掛 Topmost、後繪製者掛 Wallpaper。
    // PaintHitOrder 只問「繪製順序」，不重新依 SurfaceLayer rank 排序——保證的是「繪製層序」
    // 本身與命中一致，而非重新引入另一套（可能矛盾的）具名圖層排序。
    order.set_paint_order({
        make_solid_surface("painted-first", SurfaceLayer::Topmost),
        make_solid_surface("painted-last", SurfaceLayer::Wallpaper),
    });

    const auto r = order.hit_topmost(kCenter);
    ASSERT_TRUE(r.hit);
    EXPECT_EQ(r.id, "painted-last");  // 繪製序決定命中，不是具名圖層 rank
}

// -----------------------------------------------------------------------------
// 與 E1-04 一致：繪製順序 = 具名圖層 + 宣告順序時，兩者結果一致
// -----------------------------------------------------------------------------

TEST(ConsistencyWithE1_04, MatchesHitTesterTopmostHitWhenPaintOrderFollowsLayers) {
    // 依 E1-04 的具名圖層語意序（Wallpaper < BelowNormal < Normal < Overlay < Topmost）
    // 由底到頂宣告，此時「繪製順序」與「具名圖層 + 宣告順序」重合。
    std::vector<HitSurface> surfaces = {
        make_solid_surface("wall", SurfaceLayer::Wallpaper),
        make_solid_surface("below", SurfaceLayer::BelowNormal),
        make_solid_surface("normal", SurfaceLayer::Normal),
        make_solid_surface("overlay", SurfaceLayer::Overlay),
        make_solid_surface("top", SurfaceLayer::Topmost),
    };

    PaintHitOrder order;
    order.set_paint_order(surfaces);
    const auto via_paint_order = order.hit_topmost(kCenter);

    HitTester tester;
    const auto via_e1_04 = tester.topmost_hit(kCenter, surfaces);

    ASSERT_EQ(via_paint_order.status, HitStatus::Ok);
    ASSERT_EQ(via_e1_04.status, HitStatus::Ok);
    EXPECT_EQ(via_paint_order.hit, via_e1_04.hit);
    EXPECT_EQ(via_paint_order.id, via_e1_04.id);
    EXPECT_EQ(via_e1_04.id, "top");
}

// -----------------------------------------------------------------------------
// 順序變更後命中隨之變
// -----------------------------------------------------------------------------

TEST(ReorderChangesHit, SetPaintOrderAgainChangesHitResult) {
    PaintHitOrder order;
    order.set_paint_order({
        make_solid_surface("a"),
        make_solid_surface("b"),
    });
    auto before = order.hit_topmost(kCenter);
    ASSERT_TRUE(before.hit);
    EXPECT_EQ(before.id, "b");  // b 最後繪製，原為最上層

    // 重新設定繪製順序：交換次序，a 變成最後繪製（視覺最上層）。
    order.set_paint_order({
        make_solid_surface("b"),
        make_solid_surface("a"),
    });
    auto after = order.hit_topmost(kCenter);
    ASSERT_TRUE(after.hit);
    EXPECT_EQ(after.id, "a");  // 命中結果隨繪製順序改變而改變
    EXPECT_NE(before.id, after.id);
}

// -----------------------------------------------------------------------------
// 穩健性：空繪製序 / 無效形狀報錯不靜默 / 命中穿透
// -----------------------------------------------------------------------------

TEST(Robustness, EmptyPaintOrderNeverHits) {
    PaintHitOrder order;
    EXPECT_EQ(order.size(), 0u);
    const auto r = order.hit_topmost(kCenter);
    EXPECT_EQ(r.status, HitStatus::Ok);
    EXPECT_FALSE(r.hit);
}

TEST(Robustness, InvalidShapeInPaintOrderPropagatesInvalid) {
    PaintHitOrder order;
    HitSurface bad = make_solid_surface("bad");
    bad.shape = make_rect(-1.0f, 10.0f);  // 負範圍：無效形狀
    order.set_paint_order({make_solid_surface("good"), bad});

    const auto r = order.hit_topmost(kCenter);
    EXPECT_EQ(r.status, HitStatus::Invalid);  // 報錯不靜默
    EXPECT_FALSE(r.hit);
}

TEST(Robustness, TransparentTopSurfaceFallsThroughToLower) {
    PaintHitOrder order;
    HitSurface top = make_solid_surface("top-transparent");
    top.hit = HitPolicy::Transparent;  // 命中穿透：永不命中本 surface
    order.set_paint_order({make_solid_surface("under"), top});

    const auto r = order.hit_topmost(kCenter);
    ASSERT_TRUE(r.hit);
    EXPECT_EQ(r.id, "under");  // 最上層命中穿透，落到其下
}

TEST(Robustness, PaintOrderAccessorReflectsCurrentOrder) {
    PaintHitOrder order;
    order.set_paint_order({make_solid_surface("x"), make_solid_surface("y")});
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order.paint_order()[0].id, "x");
    EXPECT_EQ(order.paint_order()[1].id, "y");
}
