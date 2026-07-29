// E1-05 具名碰撞區域 → 事件參數 — 單元測試（gtest）
//
// 驗證 NamedRegionMap 於相位 1（Mac / null 期）建於 E1-04 HitTester 之上的具名碰撞區域：
//   - 命中具名區域回名 + 參數；多區域；重疊依加入序（後加入者為上）；無命中回結構化空結果
//   - 以 E1-04 各形狀（矩形 / 圓角矩形 / 圓 / 多邊形 / path）當作具名區域
//   - 無效形狀（負範圍 / 非有限值 / 頂點不足）於 add_region 時報錯不靜默（回 false，不新增）
//   - 空具名 / 重複具名於 add_region 時拒絕（回 false）
//   - 移除區域（remove_region）：存在則移除並回 true，未知具名回 false 不崩潰
//   - 非有限查詢點於 hit() 報錯不靜默（status = Invalid）
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實視窗系統 / 繪圖 API。
#include "named_region_map.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <string>

using ds::kernel::FillRule;
using ds::kernel::HitStatus;
using ds::kernel::LocalPoint;
using ds::kernel::make_circle;
using ds::kernel::make_path;
using ds::kernel::make_polygon;
using ds::kernel::make_rect;
using ds::kernel::make_rounded_rect;
using ds::kernel::NamedRegionMap;
using ds::kernel::RegionHit;
using ds::kernel::RegionParams;

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

// -----------------------------------------------------------------------------
// 基本命中：具名區域回名 + 參數
// -----------------------------------------------------------------------------

TEST(NamedRegion, HitReturnsNameAndParams) {
    NamedRegionMap map;
    RegionParams params;
    params["action"] = std::string("close");
    params["code"] = std::int64_t(42);
    ASSERT_TRUE(map.add_region("button.close", make_rect(10.0f, 10.0f), params));

    RegionHit r = map.hit(LocalPoint{5.0f, 5.0f});
    EXPECT_EQ(r.status, HitStatus::Ok);
    EXPECT_TRUE(r.hit);
    EXPECT_EQ(r.name, "button.close");
    ASSERT_TRUE(r.params.count("action"));
    EXPECT_EQ(std::get<std::string>(r.params.at("action")), "close");
    ASSERT_TRUE(r.params.count("code"));
    EXPECT_EQ(std::get<std::int64_t>(r.params.at("code")), 42);
}

// 未命中：結構化空結果（status=Ok, hit=false, name/params 皆空）。
TEST(NamedRegion, NoHitIsStructuredEmpty) {
    NamedRegionMap map;
    RegionParams params;
    params["action"] = std::string("close");
    ASSERT_TRUE(map.add_region("button.close", make_rect(10.0f, 10.0f), params));

    RegionHit r = map.hit(LocalPoint{99.0f, 99.0f});
    EXPECT_EQ(r.status, HitStatus::Ok);
    EXPECT_FALSE(r.hit);
    EXPECT_TRUE(r.name.empty());
    EXPECT_TRUE(r.params.empty());
}

// 空 map：無命中，不崩潰。
TEST(NamedRegion, EmptyMapIsNoHit) {
    NamedRegionMap map;
    RegionHit r = map.hit(LocalPoint{0.0f, 0.0f});
    EXPECT_EQ(r.status, HitStatus::Ok);
    EXPECT_FALSE(r.hit);
    EXPECT_EQ(map.region_count(), 0u);
}

// -----------------------------------------------------------------------------
// 多區域（按鈕不同部位 / 地圖不同區塊）
// -----------------------------------------------------------------------------

// 三個具名區域各自持有不同參數，模擬「按鈕的不同部位」各自帶不同行為代碼。
TEST(NamedRegion, MultipleRegionsCarryDistinctParams) {
    NamedRegionMap map;
    RegionParams icon_params;
    icon_params["action"] = std::string("toggle-icon");
    RegionParams label_params;
    label_params["action"] = std::string("open-menu");

    ASSERT_TRUE(map.add_region("button.icon", make_circle(LocalPoint{2.0f, 5.0f}, 2.0f),
                               icon_params));
    ASSERT_TRUE(
        map.add_region("button.label", make_circle(LocalPoint{20.0f, 5.0f}, 2.0f),
                       label_params));

    RegionHit icon_hit = map.hit(LocalPoint{2.0f, 5.0f});
    EXPECT_TRUE(icon_hit.hit);
    EXPECT_EQ(icon_hit.name, "button.icon");
    EXPECT_EQ(std::get<std::string>(icon_hit.params.at("action")), "toggle-icon");

    RegionHit label_hit = map.hit(LocalPoint{20.0f, 5.0f});
    EXPECT_TRUE(label_hit.hit);
    EXPECT_EQ(label_hit.name, "button.label");
    EXPECT_EQ(std::get<std::string>(label_hit.params.at("action")), "open-menu");
}

// 明確不重疊：以查詢點只落在其中一個區域驗證各自命中正確具名。
TEST(NamedRegion, DisjointRegionsHitCorrectName) {
    NamedRegionMap map;
    ASSERT_TRUE(map.add_region("region.a", make_circle(LocalPoint{0.0f, 0.0f}, 2.0f)));
    ASSERT_TRUE(map.add_region("region.b", make_circle(LocalPoint{20.0f, 20.0f}, 2.0f)));

    EXPECT_EQ(map.hit(LocalPoint{0.0f, 0.0f}).name, "region.a");
    EXPECT_TRUE(map.hit(LocalPoint{0.0f, 0.0f}).hit);
    EXPECT_EQ(map.hit(LocalPoint{20.0f, 20.0f}).name, "region.b");
    EXPECT_TRUE(map.hit(LocalPoint{20.0f, 20.0f}).hit);
    // 兩圓之間：都不命中。
    EXPECT_FALSE(map.hit(LocalPoint{10.0f, 10.0f}).hit);
}

// -----------------------------------------------------------------------------
// 重疊區域：依加入序（後加入者為上）
// -----------------------------------------------------------------------------

TEST(NamedRegion, OverlappingRegionsLaterAddedWins) {
    NamedRegionMap map;
    ASSERT_TRUE(map.add_region("region.under", make_rect(10.0f, 10.0f)));
    ASSERT_TRUE(map.add_region("region.over", make_rect(10.0f, 10.0f)));  // 完全重疊，後加入

    RegionHit r = map.hit(LocalPoint{5.0f, 5.0f});
    EXPECT_TRUE(r.hit);
    EXPECT_EQ(r.name, "region.over");  // 後加入者為上
}

// 移除「後加入者」後，命中回落到較早加入者。
TEST(NamedRegion, RemovingTopRegionFallsToUnder) {
    NamedRegionMap map;
    ASSERT_TRUE(map.add_region("region.under", make_rect(10.0f, 10.0f)));
    ASSERT_TRUE(map.add_region("region.over", make_rect(10.0f, 10.0f)));

    ASSERT_TRUE(map.remove_region("region.over"));
    RegionHit r = map.hit(LocalPoint{5.0f, 5.0f});
    EXPECT_TRUE(r.hit);
    EXPECT_EQ(r.name, "region.under");
}

// -----------------------------------------------------------------------------
// 以 E1-04 各形狀作為具名區域
// -----------------------------------------------------------------------------

TEST(NamedRegion, RoundedRectShapeRegion) {
    NamedRegionMap map;
    ASSERT_TRUE(map.add_region("panel.rounded", make_rounded_rect(10.0f, 10.0f, 3.0f)));
    EXPECT_TRUE(map.hit(LocalPoint{5.0f, 5.0f}).hit);   // 中心
    EXPECT_FALSE(map.hit(LocalPoint{0.0f, 0.0f}).hit);  // 角落被裁掉
}

TEST(NamedRegion, PolygonShapeRegion) {
    NamedRegionMap map;
    ASSERT_TRUE(map.add_region(
        "map.zone", make_polygon({{0.0f, 0.0f}, {4.0f, 0.0f}, {0.0f, 4.0f}})));
    EXPECT_TRUE(map.hit(LocalPoint{1.0f, 1.0f}).hit);
    EXPECT_FALSE(map.hit(LocalPoint{3.0f, 3.0f}).hit);
}

TEST(NamedRegion, PathShapeRegion) {
    NamedRegionMap map;
    ASSERT_TRUE(map.add_region(
        "map.zone.path",
        make_path({{0.0f, 0.0f}, {4.0f, 0.0f}, {4.0f, 4.0f}, {0.0f, 4.0f}},
                  FillRule::NonZero)));
    EXPECT_TRUE(map.hit(LocalPoint{2.0f, 2.0f}).hit);
    EXPECT_FALSE(map.hit(LocalPoint{5.0f, 2.0f}).hit);
}

// -----------------------------------------------------------------------------
// 無效輸入：報錯不靜默
// -----------------------------------------------------------------------------

// 無效形狀（負範圍）於 add_region 時拒絕新增，不進入 map。
TEST(NamedRegion, InvalidShapeRejectedAtAdd) {
    NamedRegionMap map;
    EXPECT_FALSE(map.add_region("bad.rect", make_rect(-1.0f, 4.0f)));
    EXPECT_FALSE(map.has_region("bad.rect"));
    EXPECT_EQ(map.region_count(), 0u);
}

// 非有限值（無限大寬度）同樣拒絕。
TEST(NamedRegion, NonFiniteShapeRejectedAtAdd) {
    NamedRegionMap map;
    EXPECT_FALSE(map.add_region("bad.inf", make_rect(kInf, 4.0f)));
    EXPECT_EQ(map.region_count(), 0u);
}

// 頂點數不足（多邊形 < 3）拒絕。
TEST(NamedRegion, InsufficientVerticesRejectedAtAdd) {
    NamedRegionMap map;
    EXPECT_FALSE(map.add_region("bad.poly", make_polygon({{0.0f, 0.0f}, {1.0f, 1.0f}})));
    EXPECT_EQ(map.region_count(), 0u);
}

// 空具名拒絕新增。
TEST(NamedRegion, EmptyNameRejected) {
    NamedRegionMap map;
    EXPECT_FALSE(map.add_region("", make_rect(4.0f, 4.0f)));
    EXPECT_EQ(map.region_count(), 0u);
}

// 重複具名拒絕新增（原區域保持不變）。
TEST(NamedRegion, DuplicateNameRejected) {
    NamedRegionMap map;
    ASSERT_TRUE(map.add_region("region.a", make_rect(4.0f, 4.0f)));
    EXPECT_FALSE(map.add_region("region.a", make_circle(LocalPoint{0.0f, 0.0f}, 2.0f)));
    EXPECT_EQ(map.region_count(), 1u);
    // 原區域仍為矩形（未被覆寫）：邊界外一點 (4,4) 內矩形命中，圓半徑 2 不會涵蓋。
    EXPECT_TRUE(map.hit(LocalPoint{4.0f, 4.0f}).hit);
}

// 非有限查詢點：status = Invalid（報錯不靜默）。
TEST(NamedRegion, NonFiniteQueryPointReportsInvalid) {
    NamedRegionMap map;
    ASSERT_TRUE(map.add_region("region.a", make_rect(4.0f, 4.0f)));

    RegionHit r = map.hit(LocalPoint{kNaN, 1.0f});
    EXPECT_EQ(r.status, HitStatus::Invalid);
    EXPECT_FALSE(r.hit);

    RegionHit r2 = map.hit(LocalPoint{1.0f, kInf});
    EXPECT_EQ(r2.status, HitStatus::Invalid);
}

// -----------------------------------------------------------------------------
// 移除區域
// -----------------------------------------------------------------------------

TEST(NamedRegion, RemoveRegionSucceedsAndUpdatesState) {
    NamedRegionMap map;
    ASSERT_TRUE(map.add_region("region.a", make_rect(4.0f, 4.0f)));
    EXPECT_TRUE(map.has_region("region.a"));
    EXPECT_EQ(map.region_count(), 1u);

    EXPECT_TRUE(map.remove_region("region.a"));
    EXPECT_FALSE(map.has_region("region.a"));
    EXPECT_EQ(map.region_count(), 0u);
    EXPECT_FALSE(map.hit(LocalPoint{1.0f, 1.0f}).hit);
}

// 移除未知具名：回 false，不崩潰。
TEST(NamedRegion, RemoveUnknownNameReturnsFalse) {
    NamedRegionMap map;
    EXPECT_FALSE(map.remove_region("no.such.region"));
}

// 移除後可用同名重新新增（具名重複限制只在「同時存在」時生效）。
TEST(NamedRegion, NameReusableAfterRemoval) {
    NamedRegionMap map;
    ASSERT_TRUE(map.add_region("region.a", make_rect(4.0f, 4.0f)));
    ASSERT_TRUE(map.remove_region("region.a"));
    EXPECT_TRUE(map.add_region("region.a", make_circle(LocalPoint{0.0f, 0.0f}, 2.0f)));
    EXPECT_EQ(map.region_count(), 1u);
}

}  // namespace
