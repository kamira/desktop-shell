// E1-09 邊緣吸附 — 單元測試（gtest）
//
// 驗證「拖曳中 surface 對螢幕邊 / 其他 surface 邊自動吸附對齊」的純幾何邏輯與宣告式再表達：
//   - 靠近螢幕四邊 → 吸附（左 / 右 / 上 / 下），以具名角 / 邊錨點 + 零偏移承載（隨容器縮放恆貼齊）
//   - 閾值內吸附、閾值外不吸附（邊界含等於）
//   - 吸附其他 surface 的邊；多目標選最近
//   - 遠離不吸附（位置原樣，round-trip 回同一矩形）
//   - 可設閾值（正規化分數 / 具名間距）、可分別開關螢幕 / surface 來源
//   - 與 E1-08 整合：讀拖曳中 surface 的實時位置吸附、回寫 drag_to、end_drag 記住吸附後位置
//   - 無效輸入結構化回報（越界 anchor / 非有限 / 負尺寸 / 非有限 / 負閾值 / 非正容器）→ Invalid
//   - NFR-02：吸附後位置全為 AnchorSpec（具名錨點 + 正規化偏移），具體像素只在 resolve 邊界
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）、無真實視窗 / 繪圖 API。
#include "edge_snapping.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

using ds::kernel::Anchor;
using ds::kernel::AnchorSpec;
using ds::kernel::AnchorStatus;
using ds::kernel::AxisSnap;
using ds::kernel::DraggableSurface;
using ds::kernel::DragStatus;
using ds::kernel::Edge;
using ds::kernel::EdgeSnapping;
using ds::kernel::NullKernelBackend;
using ds::kernel::Offset;
using ds::kernel::resolve;
using ds::kernel::ResolvedPlacement;
using ds::kernel::screen_edge_of;
using ds::kernel::Size;
using ds::kernel::snap;
using ds::kernel::snap_rect;
using ds::kernel::snap_surface;
using ds::kernel::SnapConfig;
using ds::kernel::SnapResult;
using ds::kernel::SnapStatus;
using ds::kernel::SnapTarget;
using ds::kernel::Spacing;
using ds::kernel::SurfaceId;
using ds::kernel::SurfaceProfile;
using ds::kernel::SurfaceTarget;

namespace {

constexpr float kEps = 1e-4f;

AnchorSpec spec_of(Anchor a, float dx = 0.0f, float dy = 0.0f) {
    AnchorSpec s;
    s.anchor = a;
    s.offset = Offset{dx, dy};
    return s;
}

ResolvedPlacement rect_of(float x, float y, float w, float h) {
    ResolvedPlacement r;
    r.x = x;
    r.y = y;
    r.width = w;
    r.height = h;
    return r;
}

NullKernelBackend make_backend(const std::vector<SurfaceId>& ids) {
    NullKernelBackend backend;
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    for (const auto& id : ids) {
        backend.create_surface(id, SurfaceProfile{});
    }
    return backend;
}

// -----------------------------------------------------------------------------
// 純幾何核心：吸附螢幕四邊
// -----------------------------------------------------------------------------

TEST(SnapRect, SnapsToEachScreenEdge) {
    const Size container{1000.0f, 800.0f};
    SnapConfig cfg;  // threshold 0.02 → x 判定距離 20、y 判定距離 16
    SnapResult r;

    // 左邊：左緣在 x=10（≤20）→ 吸到 0。
    ASSERT_EQ(snap_rect(rect_of(10.0f, 400.0f, 100.0f, 100.0f), {}, container, cfg, r),
              SnapStatus::Ok);
    EXPECT_EQ(r.x, AxisSnap::ScreenStart);
    EXPECT_NEAR(r.rect.x, 0.0f, kEps);

    // 右邊：右緣在 x+ w = 995（距 1000 為 5 ≤20）→ 左緣移到 900。
    ASSERT_EQ(snap_rect(rect_of(895.0f, 400.0f, 100.0f, 100.0f), {}, container, cfg, r),
              SnapStatus::Ok);
    EXPECT_EQ(r.x, AxisSnap::ScreenEnd);
    EXPECT_NEAR(r.rect.x, 900.0f, kEps);

    // 上邊：上緣在 y=8（≤16）→ 吸到 0。
    ASSERT_EQ(snap_rect(rect_of(400.0f, 8.0f, 100.0f, 100.0f), {}, container, cfg, r),
              SnapStatus::Ok);
    EXPECT_EQ(r.y, AxisSnap::ScreenStart);
    EXPECT_NEAR(r.rect.y, 0.0f, kEps);

    // 下邊：下緣在 y+h = 790（距 800 為 10 ≤16）→ 上緣移到 700。
    ASSERT_EQ(snap_rect(rect_of(400.0f, 690.0f, 100.0f, 100.0f), {}, container, cfg, r),
              SnapStatus::Ok);
    EXPECT_EQ(r.y, AxisSnap::ScreenEnd);
    EXPECT_NEAR(r.rect.y, 700.0f, kEps);
}

TEST(SnapRect, ThresholdBoundaryInAndOut) {
    const Size container{1000.0f, 1000.0f};
    SnapConfig cfg;
    cfg.threshold = 0.02f;  // 判定距離 = 20
    SnapResult r;

    // 距 20（剛好等於閾值）→ 吸附（含等於）。
    ASSERT_EQ(snap_rect(rect_of(20.0f, 500.0f, 50.0f, 50.0f), {}, container, cfg, r), SnapStatus::Ok);
    EXPECT_EQ(r.x, AxisSnap::ScreenStart);
    EXPECT_NEAR(r.rect.x, 0.0f, kEps);

    // 距 21（超過閾值）→ 不吸附，位置原樣。
    ASSERT_EQ(snap_rect(rect_of(21.0f, 500.0f, 50.0f, 50.0f), {}, container, cfg, r), SnapStatus::Ok);
    EXPECT_EQ(r.x, AxisSnap::None);
    EXPECT_NEAR(r.rect.x, 21.0f, kEps);
}

TEST(SnapRect, FarAwayDoesNotSnap) {
    const Size container{1000.0f, 1000.0f};
    SnapConfig cfg;
    SnapResult r;
    ASSERT_EQ(snap_rect(rect_of(500.0f, 500.0f, 100.0f, 100.0f), {}, container, cfg, r),
              SnapStatus::Ok);
    EXPECT_FALSE(r.snapped());
    EXPECT_EQ(r.x, AxisSnap::None);
    EXPECT_EQ(r.y, AxisSnap::None);
    EXPECT_NEAR(r.rect.x, 500.0f, kEps);
    EXPECT_NEAR(r.rect.y, 500.0f, kEps);
}

// -----------------------------------------------------------------------------
// 純幾何核心：吸附其他 surface 邊 / 多目標選最近
// -----------------------------------------------------------------------------

TEST(SnapRect, SnapsToSurfaceEdge) {
    const Size container{2000.0f, 2000.0f};  // 判定距離 40，遠離螢幕邊以隔離 surface 吸附
    SnapConfig cfg;
    SnapResult r;

    // target 在 [500,600]（寬 100）；dragged 寬 80、左緣 490（距 target 左緣 500 為 10；
    // 右-右對齊距 30、更遠）→ 取最近的左-左對齊到 500。
    const ResolvedPlacement target = rect_of(500.0f, 500.0f, 100.0f, 100.0f);
    ASSERT_EQ(snap_rect(rect_of(490.0f, 900.0f, 80.0f, 80.0f), {target}, container, cfg, r),
              SnapStatus::Ok);
    EXPECT_EQ(r.x, AxisSnap::Surface);
    EXPECT_NEAR(r.rect.x, 500.0f, kEps);

    // 相鄰：dragged 左緣貼 target 右緣（600）。dragged 左緣 620（距 600 為 20 ≤40）→ 移到 600。
    ASSERT_EQ(snap_rect(rect_of(620.0f, 900.0f, 80.0f, 80.0f), {target}, container, cfg, r),
              SnapStatus::Ok);
    EXPECT_EQ(r.x, AxisSnap::Surface);
    EXPECT_NEAR(r.rect.x, 600.0f, kEps);
}

TEST(SnapRect, MultipleTargetsPicksNearestEdge) {
    const Size container{5000.0f, 5000.0f};  // 判定距離 100
    SnapConfig cfg;
    SnapResult r;

    // 兩個 target 左緣：near 在 1000、far 在 1050。dragged 左緣 1040：距 near=40、距 far=10 → 選 far(1050)。
    const ResolvedPlacement near_t = rect_of(1000.0f, 2000.0f, 100.0f, 100.0f);
    const ResolvedPlacement far_t = rect_of(1050.0f, 3000.0f, 100.0f, 100.0f);
    ASSERT_EQ(snap_rect(rect_of(1040.0f, 4000.0f, 80.0f, 80.0f), {near_t, far_t}, container, cfg, r),
              SnapStatus::Ok);
    EXPECT_EQ(r.x, AxisSnap::Surface);
    EXPECT_NEAR(r.rect.x, 1050.0f, kEps);  // 對齊到較近那條邊
}

TEST(SnapRect, ConfigDisablesSources) {
    const Size container{1000.0f, 1000.0f};
    SnapResult r;

    // 關閉螢幕吸附：靠左也不吸。
    SnapConfig no_screen;
    no_screen.to_screen = false;
    ASSERT_EQ(snap_rect(rect_of(5.0f, 500.0f, 50.0f, 50.0f), {}, container, no_screen, r),
              SnapStatus::Ok);
    EXPECT_EQ(r.x, AxisSnap::None);
    EXPECT_NEAR(r.rect.x, 5.0f, kEps);

    // 關閉 surface 吸附：靠近 target 也不吸（但螢幕仍作用）。
    SnapConfig no_surf;
    no_surf.to_surfaces = false;
    const ResolvedPlacement target = rect_of(500.0f, 500.0f, 100.0f, 100.0f);
    ASSERT_EQ(snap_rect(rect_of(510.0f, 500.0f, 50.0f, 50.0f), {target}, container, no_surf, r),
              SnapStatus::Ok);
    EXPECT_EQ(r.x, AxisSnap::None);
    EXPECT_NEAR(r.rect.x, 510.0f, kEps);
}

// -----------------------------------------------------------------------------
// 純幾何核心：無效輸入結構化回報
// -----------------------------------------------------------------------------

TEST(SnapRect, InvalidInputsRejected) {
    const Size container{1000.0f, 1000.0f};
    SnapConfig cfg;
    SnapResult r;
    const ResolvedPlacement ok = rect_of(10.0f, 10.0f, 50.0f, 50.0f);

    // 非有限容器。
    EXPECT_EQ(snap_rect(ok, {}, Size{std::numeric_limits<float>::infinity(), 1000.0f}, cfg, r),
              SnapStatus::Invalid);
    // 負閾值。
    SnapConfig neg = cfg;
    neg.threshold = -0.01f;
    EXPECT_EQ(snap_rect(ok, {}, container, neg, r), SnapStatus::Invalid);
    // 非有限閾值。
    SnapConfig nan_thr = cfg;
    nan_thr.threshold = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(snap_rect(ok, {}, container, nan_thr, r), SnapStatus::Invalid);
    // 非有限 dragged 矩形。
    EXPECT_EQ(snap_rect(rect_of(std::numeric_limits<float>::quiet_NaN(), 0.0f, 10.0f, 10.0f), {},
                        container, cfg, r),
              SnapStatus::Invalid);
    // 無效目標（負尺寸）。
    EXPECT_EQ(snap_rect(ok, {rect_of(0.0f, 0.0f, -1.0f, 10.0f)}, container, cfg, r),
              SnapStatus::Invalid);
}

// -----------------------------------------------------------------------------
// 宣告式吸附：再表達為 AnchorSpec + round-trip + NFR-02 縮放
// -----------------------------------------------------------------------------

TEST(SnapDeclarative, ScreenEdgeBecomesNamedAnchorZeroOffset) {
    const Size container{1000.0f, 800.0f};
    const Size element{100.0f, 100.0f};
    SnapConfig cfg;
    AnchorSpec out;

    // 靠右下：以 TopLeft + 大偏移放到接近右下角，兩軸都在閾值內 → 吸到 BottomRight + 零偏移。
    // 左緣 = 0.895*1000 = 895（右緣 995，距 1000 為 5）；上緣 = 0.865*800 = 692（下緣 792，距 800 為 8）。
    const AnchorSpec dragged = spec_of(Anchor::TopLeft, 0.895f, 0.865f);
    ASSERT_EQ(snap(dragged, element, {}, container, cfg, out), SnapStatus::Ok);
    EXPECT_EQ(static_cast<int>(out.anchor), static_cast<int>(Anchor::BottomRight));
    EXPECT_NEAR(out.offset.dx, 0.0f, kEps);
    EXPECT_NEAR(out.offset.dy, 0.0f, kEps);

    // round-trip：吸附後 spec 落地應貼右下角。
    ResolvedPlacement p;
    ASSERT_EQ(resolve(out, container, element, p), AnchorStatus::Ok);
    EXPECT_NEAR(p.x, 900.0f, kEps);
    EXPECT_NEAR(p.y, 700.0f, kEps);
}

TEST(SnapDeclarative, StickyAcrossContainerResize) {
    // 吸右邊後以具名 Right 錨承載 → 不同容器下恆貼右緣（NFR-02：相對，不硬編像素）。
    const Size element{100.0f, 100.0f};
    SnapConfig cfg;
    AnchorSpec out;
    // 左緣 0.895*1000=895、右緣 995（距 1000 為 5 ≤20）→ 吸右；y 置中不吸。
    const AnchorSpec dragged = spec_of(Anchor::TopLeft, 0.895f, 0.45f);
    ASSERT_EQ(snap(dragged, element, {}, Size{1000.0f, 800.0f}, cfg, out), SnapStatus::Ok);
    EXPECT_NEAR(out.offset.dx, 0.0f, kEps);  // 貼右緣 → 零偏移

    // 同一吸附後 spec 在更大容器仍貼右緣。
    ResolvedPlacement big;
    ASSERT_EQ(resolve(out, Size{2000.0f, 1200.0f}, element, big), AnchorStatus::Ok);
    EXPECT_NEAR(big.x, 2000.0f - 100.0f, kEps);
}

TEST(SnapDeclarative, FarPositionPreservedRoundTrip) {
    const Size container{1000.0f, 1000.0f};
    const Size element{100.0f, 100.0f};
    SnapConfig cfg;
    AnchorSpec out;
    // 置中，遠離所有邊 → 不吸附；吸附後 spec 落地應與原位置一致。
    const AnchorSpec dragged = spec_of(Anchor::Center);
    ResolvedPlacement before;
    ASSERT_EQ(resolve(dragged, container, element, before), AnchorStatus::Ok);
    ASSERT_EQ(snap(dragged, element, {}, container, cfg, out), SnapStatus::Ok);
    ResolvedPlacement after;
    ASSERT_EQ(resolve(out, container, element, after), AnchorStatus::Ok);
    EXPECT_NEAR(after.x, before.x, kEps);
    EXPECT_NEAR(after.y, before.y, kEps);
}

TEST(SnapDeclarative, SnapToSurfaceTargetSpec) {
    const Size container{2000.0f, 2000.0f};  // 判定距離 40
    const Size element{80.0f, 80.0f};
    SnapConfig cfg;
    AnchorSpec out;

    // target：TopLeft + dx 0.25 → 左緣 500（寬 100）。dragged 寬 80、左緣 0.245*2000=490
    // （距 target 左緣 500 為 10；右-右更遠）→ 最近的左-左對齊到 500。
    SnapTarget target;
    target.spec = spec_of(Anchor::TopLeft, 0.25f, 0.45f);  // 左緣 500
    target.element = Size{100.0f, 100.0f};
    const AnchorSpec dragged = spec_of(Anchor::TopLeft, 0.245f, 0.45f);
    ASSERT_EQ(snap(dragged, element, {target}, container, cfg, out), SnapStatus::Ok);
    ResolvedPlacement p;
    ASSERT_EQ(resolve(out, container, element, p), AnchorStatus::Ok);
    EXPECT_NEAR(p.x, 500.0f, kEps);  // 對齊 target 左緣
}

TEST(SnapDeclarative, InvalidInputsRejected) {
    const Size container{1000.0f, 1000.0f};
    const Size element{100.0f, 100.0f};
    SnapConfig cfg;
    AnchorSpec out;

    // 越界 anchor。
    AnchorSpec bad = spec_of(Anchor::Center);
    bad.anchor = static_cast<Anchor>(99);
    EXPECT_EQ(snap(bad, element, {}, container, cfg, out), SnapStatus::Invalid);
    // 非有限 offset。
    EXPECT_EQ(snap(spec_of(Anchor::Center, std::numeric_limits<float>::infinity()), element, {},
                   container, cfg, out),
              SnapStatus::Invalid);
    // 非正容器（0 寬）→ Invalid（無法換算正規化偏移）。
    EXPECT_EQ(snap(spec_of(Anchor::Center), element, {}, Size{0.0f, 1000.0f}, cfg, out),
              SnapStatus::Invalid);
    // 無效目標（越界 anchor）。
    SnapTarget bt;
    bt.spec = spec_of(Anchor::Center);
    bt.spec.anchor = static_cast<Anchor>(-1);
    bt.element = element;
    EXPECT_EQ(snap(spec_of(Anchor::Center), element, {bt}, container, cfg, out),
              SnapStatus::Invalid);
}

// -----------------------------------------------------------------------------
// 設定：具名間距閾值 / EdgeSnapping 服務
// -----------------------------------------------------------------------------

TEST(Config, FromSpacingThreshold) {
    // Roomy = 0.08；1000 寬容器 → 判定距離 80。
    const SnapConfig cfg = SnapConfig::from_spacing(Spacing::Roomy);
    EXPECT_NEAR(cfg.threshold, 0.08f, kEps);
    const Size container{1000.0f, 1000.0f};
    SnapResult r;
    // 左緣 70（≤80）→ 吸；用小閾值（Snug=0.02→20）則不吸。
    ASSERT_EQ(snap_rect(rect_of(70.0f, 500.0f, 50.0f, 50.0f), {}, container, cfg, r), SnapStatus::Ok);
    EXPECT_EQ(r.x, AxisSnap::ScreenStart);

    const SnapConfig tight = SnapConfig::from_spacing(Spacing::Snug);
    ASSERT_EQ(snap_rect(rect_of(70.0f, 500.0f, 50.0f, 50.0f), {}, container, tight, r),
              SnapStatus::Ok);
    EXPECT_EQ(r.x, AxisSnap::None);
}

TEST(Config, EdgeSnappingServiceUsesConfig) {
    const Size container{1000.0f, 800.0f};
    const Size element{100.0f, 100.0f};
    EdgeSnapping snapping{SnapConfig::from_spacing(Spacing::Tight)};  // 0.01 → 10
    EXPECT_NEAR(snapping.config().threshold, 0.01f, kEps);
    AnchorSpec out;
    // 左緣在 0.005*1000 = 5（≤10）→ 吸左。
    ASSERT_EQ(snapping.snap(spec_of(Anchor::TopLeft, 0.005f, 0.45f), element, {}, container, out),
              SnapStatus::Ok);
    ResolvedPlacement p;
    ASSERT_EQ(resolve(out, container, element, p), AnchorStatus::Ok);
    EXPECT_NEAR(p.x, 0.0f, kEps);
}

// -----------------------------------------------------------------------------
// 具名邊查詢
// -----------------------------------------------------------------------------

TEST(NamedEdge, ScreenEdgeOfAxisResult) {
    Edge e;
    ASSERT_TRUE(screen_edge_of(AxisSnap::ScreenStart, true, e));
    EXPECT_EQ(static_cast<int>(e), static_cast<int>(Edge::Left));
    ASSERT_TRUE(screen_edge_of(AxisSnap::ScreenEnd, true, e));
    EXPECT_EQ(static_cast<int>(e), static_cast<int>(Edge::Right));
    ASSERT_TRUE(screen_edge_of(AxisSnap::ScreenStart, false, e));
    EXPECT_EQ(static_cast<int>(e), static_cast<int>(Edge::Top));
    ASSERT_TRUE(screen_edge_of(AxisSnap::ScreenEnd, false, e));
    EXPECT_EQ(static_cast<int>(e), static_cast<int>(Edge::Bottom));
    // None / Surface 無具名螢幕邊 → false 且不觸碰 out。
    Edge untouched = Edge::Left;
    EXPECT_FALSE(screen_edge_of(AxisSnap::None, true, untouched));
    EXPECT_EQ(static_cast<int>(untouched), static_cast<int>(Edge::Left));
    EXPECT_FALSE(screen_edge_of(AxisSnap::Surface, true, untouched));
}

// -----------------------------------------------------------------------------
// 與 E1-08 整合
// -----------------------------------------------------------------------------

TEST(Integration, SnapLivePositionDuringDragThenRemember) {
    NullKernelBackend backend = make_backend({"surface.pet"});
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    DraggableSurface drag(backend);
    const Size container{1000.0f, 800.0f};
    const Size element{100.0f, 100.0f};

    // 註冊並開始拖曳，拖到靠近左邊（左緣 0.01*1000 = 10 ≤ 20）。
    ASSERT_EQ(drag.set_position("surface.pet", spec_of(Anchor::Center)), DragStatus::Ok);
    ASSERT_EQ(drag.begin_drag("surface.pet"), DragStatus::Ok);
    ASSERT_EQ(drag.drag_to("surface.pet", spec_of(Anchor::TopLeft, 0.01f, 0.30f)), DragStatus::Ok);

    // 對實時位置做吸附（無其他目標）。
    EdgeSnapping snapping;  // 預設 0.02
    AnchorSpec snapped;
    ASSERT_EQ(snap_surface(snapping, drag, "surface.pet", element, {}, container, snapped),
              SnapStatus::Ok);
    EXPECT_EQ(static_cast<int>(snapped.anchor), static_cast<int>(Anchor::TopLeft));
    EXPECT_NEAR(snapped.offset.dx, 0.0f, kEps);  // 吸左 → 零水平偏移

    // 把吸附位置回寫 drag_to、放開 → 記住吸附後位置。
    ASSERT_EQ(drag.drag_to("surface.pet", snapped), DragStatus::Ok);
    ASSERT_EQ(drag.end_drag("surface.pet"), DragStatus::Ok);
    ResolvedPlacement p;
    ASSERT_EQ(drag.resolve_live("surface.pet", container, element, p), AnchorStatus::Ok);
    EXPECT_NEAR(p.x, 0.0f, kEps);  // 貼左緣
}

TEST(Integration, SnapToAnotherSurfaceLiveEdge) {
    NullKernelBackend backend = make_backend({"surface.a", "surface.dock"});
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    DraggableSurface drag(backend);
    const Size container{2000.0f, 2000.0f};  // 判定距離 40
    const Size element{80.0f, 80.0f};

    // dock 固定於左緣 500（TopLeft + 0.25，寬 100）。
    ASSERT_EQ(drag.set_position("surface.dock", spec_of(Anchor::TopLeft, 0.25f, 0.60f)),
              DragStatus::Ok);
    // a（寬 80）拖到左緣 0.245*2000=490（距 dock 左緣 500 為 10；右-右更遠）→ 最近的左-左對齊 500。
    ASSERT_EQ(drag.set_position("surface.a", spec_of(Anchor::TopLeft, 0.245f, 0.20f)),
              DragStatus::Ok);
    ASSERT_EQ(drag.begin_drag("surface.a"), DragStatus::Ok);

    EdgeSnapping snapping;
    AnchorSpec snapped;
    std::vector<SurfaceTarget> targets{SurfaceTarget{"surface.dock", Size{100.0f, 100.0f}},
                                       SurfaceTarget{"surface.a", element}};  // 含自己 → 應略過
    ASSERT_EQ(snap_surface(snapping, drag, "surface.a", element, targets, container, snapped),
              SnapStatus::Ok);
    ResolvedPlacement p;
    ASSERT_EQ(resolve(snapped, container, element, p), AnchorStatus::Ok);
    EXPECT_NEAR(p.x, 500.0f, kEps);  // 對齊 dock 左緣
}

TEST(Integration, SnapSurfaceUntrackedIsInvalid) {
    NullKernelBackend backend = make_backend({"surface.a"});
    backend.init();  // CHG-20260803-11：create_surface 的前置條件（K-007 對齊）
    DraggableSurface drag(backend);
    EdgeSnapping snapping;
    AnchorSpec out;
    // 未註冊且未拖曳 → 無實時位置 → Invalid。
    EXPECT_EQ(snap_surface(snapping, drag, "surface.ghost", Size{10.0f, 10.0f}, {},
                           Size{1000.0f, 1000.0f}, out),
              SnapStatus::Invalid);
}

}  // namespace
