// E1-02 輸入策略四態 — gtest 單元測試
//
// 涵蓋：四態純函式（命中 / 路由 / 對映 E1-24 / 具名字串）、set/query、預設策略、
// 各態命中與跨 surface 路由行為、狀態轉換、has() 能力閘控（NFR-03）、
// 與 E1-24 K3 poll_input 整合。平台中立，無平台分支。
#include "input_strategy.hpp"

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace ds::kernel {
namespace {

// 建一個宣告 input.capture 為可用的能力矩陣（供 Capture 態測試）。
CapabilityMatrix caps_with_capture() {
    std::vector<CapabilityDecl> decls = {
        {"input.capture", "全域獨占捕捉（測試啟用）", /*optional=*/true, /*default_available=*/true},
    };
    return CapabilityMatrix(std::move(decls));
}

// 便利：在後端建立一個具名 surface。
void make_surface(NullKernelBackend& b, const SurfaceId& id) {
    ASSERT_TRUE(b.create_surface(id, SurfaceProfile{}));
}

// --- 純函式：命中測試 ---
TEST(E1_02_PureFn, HitResultPerState) {
    EXPECT_EQ(hit_result(InputStrategy::Interactive), InputHitResult::Solid);
    EXPECT_EQ(hit_result(InputStrategy::Capture), InputHitResult::Solid);
    EXPECT_EQ(hit_result(InputStrategy::ClickThrough), InputHitResult::Transparent);
    EXPECT_EQ(hit_result(InputStrategy::Inert), InputHitResult::Solid);
}

// --- 純函式：單體路由決策 ---
TEST(E1_02_PureFn, RouteDecisionPerState) {
    EXPECT_EQ(route_decision(InputStrategy::Interactive), RouteDecision::Deliver);
    EXPECT_EQ(route_decision(InputStrategy::Capture), RouteDecision::CaptureAll);
    EXPECT_EQ(route_decision(InputStrategy::ClickThrough), RouteDecision::PassBelow);
    EXPECT_EQ(route_decision(InputStrategy::Inert), RouteDecision::Swallow);
}

// --- 純函式：對齊下推到 E1-24 K3 三態 ---
TEST(E1_02_PureFn, ToBackendPolicyAlignsWithE1_24) {
    EXPECT_EQ(to_backend_policy(InputStrategy::Interactive), InputPolicy::Accepting);
    EXPECT_EQ(to_backend_policy(InputStrategy::Capture), InputPolicy::Modal);
    EXPECT_EQ(to_backend_policy(InputStrategy::ClickThrough), InputPolicy::PassThrough);
    // Inert：命中實心 → 取 Accepting；吞掉語意由路由層補足（E1-24 三態無對應）。
    EXPECT_EQ(to_backend_policy(InputStrategy::Inert), InputPolicy::Accepting);
}

// --- 純函式：具名字串（NFR-02）---
TEST(E1_02_PureFn, ToStringIsNamed) {
    EXPECT_STREQ(to_string(InputStrategy::Interactive), "Interactive");
    EXPECT_STREQ(to_string(InputStrategy::Capture), "Capture");
    EXPECT_STREQ(to_string(InputStrategy::ClickThrough), "ClickThrough");
    EXPECT_STREQ(to_string(InputStrategy::Inert), "Inert");
}

// --- 預設策略：未設定即 Interactive ---
TEST(E1_02_Default, UnsetSurfaceIsInteractive) {
    NullKernelBackend backend;
    make_surface(backend, "surface.panel");
    InputStrategyController ctl(backend);

    EXPECT_EQ(ctl.strategy("surface.panel"), kDefaultStrategy);
    EXPECT_EQ(ctl.strategy("surface.panel"), InputStrategy::Interactive);
    EXPECT_FALSE(ctl.has_strategy("surface.panel"));
    EXPECT_EQ(ctl.tracked_count(), 0u);
}

TEST(E1_02_Default, UnknownSurfaceQueryIsDefaultNotCrash) {
    NullKernelBackend backend;
    InputStrategyController ctl(backend);
    EXPECT_EQ(ctl.strategy("surface.ghost"), InputStrategy::Interactive);
    EXPECT_FALSE(ctl.has_strategy("surface.ghost"));
}

// --- 設定：surface 須存在 ---
TEST(E1_02_Set, RequiresExistingSurface) {
    NullKernelBackend backend;
    InputStrategyController ctl(backend);
    EXPECT_FALSE(ctl.set_strategy("surface.absent", InputStrategy::Interactive));
    EXPECT_FALSE(ctl.has_strategy("surface.absent"));
    EXPECT_EQ(ctl.tracked_count(), 0u);
}

// --- 設定三態（無需能力）並下推至後端 K3 ---
TEST(E1_02_Set, InteractiveClickThroughInertNoCapabilityNeeded) {
    NullKernelBackend backend;  // 預設矩陣：無 input.capture
    make_surface(backend, "surface.a");
    InputStrategyController ctl(backend);

    ASSERT_TRUE(ctl.set_strategy("surface.a", InputStrategy::Interactive));
    EXPECT_EQ(ctl.strategy("surface.a"), InputStrategy::Interactive);
    EXPECT_EQ(backend.surface_profile("surface.a")->input, InputPolicy::Accepting);

    ASSERT_TRUE(ctl.set_strategy("surface.a", InputStrategy::ClickThrough));
    EXPECT_EQ(backend.surface_profile("surface.a")->input, InputPolicy::PassThrough);

    ASSERT_TRUE(ctl.set_strategy("surface.a", InputStrategy::Inert));
    EXPECT_EQ(backend.surface_profile("surface.a")->input, InputPolicy::Accepting);

    EXPECT_TRUE(ctl.has_strategy("surface.a"));
    EXPECT_EQ(ctl.tracked_count(), 1u);  // 狀態轉換不新增追蹤項
}

// --- NFR-03：Capture 受能力閘控 ---
TEST(E1_02_Gating, CaptureRejectedWhenCapabilityAbsent) {
    NullKernelBackend backend;  // 預設矩陣未宣告 input.capture → has() 保守 false
    make_surface(backend, "surface.modal");
    InputStrategyController ctl(backend);

    ASSERT_FALSE(backend.has("input.capture"));
    EXPECT_FALSE(ctl.set_strategy("surface.modal", InputStrategy::Capture));
    // 不改任何狀態
    EXPECT_FALSE(ctl.has_strategy("surface.modal"));
    EXPECT_EQ(ctl.strategy("surface.modal"), InputStrategy::Interactive);
    EXPECT_EQ(ctl.tracked_count(), 0u);
}

TEST(E1_02_Gating, CaptureAllowedWhenCapabilityPresent) {
    NullKernelBackend backend(caps_with_capture());
    make_surface(backend, "surface.modal");
    InputStrategyController ctl(backend);

    ASSERT_TRUE(backend.has("input.capture"));
    ASSERT_TRUE(ctl.set_strategy("surface.modal", InputStrategy::Capture));
    EXPECT_EQ(ctl.strategy("surface.modal"), InputStrategy::Capture);
    EXPECT_EQ(backend.surface_profile("surface.modal")->input, InputPolicy::Modal);
}

TEST(E1_02_Gating, CaptureCapabilityIdIsNamed) {
    EXPECT_EQ(capture_capability_id(), std::string("input.capture"));
}

// --- 狀態轉換：保留堆疊位置 ---
TEST(E1_02_Transition, UpdateKeepsPositionAndTrackCount) {
    NullKernelBackend backend;
    make_surface(backend, "surface.x");
    make_surface(backend, "surface.y");
    InputStrategyController ctl(backend);

    ASSERT_TRUE(ctl.set_strategy("surface.x", InputStrategy::Interactive));
    ASSERT_TRUE(ctl.set_strategy("surface.y", InputStrategy::ClickThrough));
    EXPECT_EQ(ctl.tracked_count(), 2u);

    // 就地轉換 x：不新增、不改變 y。
    ASSERT_TRUE(ctl.set_strategy("surface.x", InputStrategy::Inert));
    EXPECT_EQ(ctl.strategy("surface.x"), InputStrategy::Inert);
    EXPECT_EQ(ctl.strategy("surface.y"), InputStrategy::ClickThrough);
    EXPECT_EQ(ctl.tracked_count(), 2u);
}

// --- forget：移除記錄 ---
TEST(E1_02_Forget, RemovesRecordAndRevertsToDefault) {
    NullKernelBackend backend;
    make_surface(backend, "surface.z");
    InputStrategyController ctl(backend);

    ASSERT_TRUE(ctl.set_strategy("surface.z", InputStrategy::ClickThrough));
    EXPECT_TRUE(ctl.has_strategy("surface.z"));

    EXPECT_TRUE(ctl.forget("surface.z"));
    EXPECT_FALSE(ctl.has_strategy("surface.z"));
    EXPECT_EQ(ctl.strategy("surface.z"), InputStrategy::Interactive);
    EXPECT_EQ(ctl.tracked_count(), 0u);

    EXPECT_FALSE(ctl.forget("surface.z"));  // 再次移除：不崩潰、回 false
}

// --- 路由：Interactive 直接遞送 ---
TEST(E1_02_Route, InteractiveDelivers) {
    NullKernelBackend backend;
    make_surface(backend, "surface.top");
    InputStrategyController ctl(backend);
    ASSERT_TRUE(ctl.set_strategy("surface.top", InputStrategy::Interactive));

    InputEvent ev;
    ev.type = InputEventType::PointerDown;
    ev.target = "surface.top";
    auto routed = ctl.route({ev});

    ASSERT_EQ(routed.size(), 1u);
    EXPECT_EQ(routed[0].decision, RouteDecision::Deliver);
    EXPECT_EQ(routed[0].delivered_to, "surface.top");
}

// --- 路由：ClickThrough 穿透到其下 ---
TEST(E1_02_Route, ClickThroughPassesToSurfaceBelow) {
    NullKernelBackend backend;
    make_surface(backend, "surface.below");
    make_surface(backend, "surface.above");
    InputStrategyController ctl(backend);
    // 登記順序 = 堆疊：below 先（底）、above 後（頂）。
    ASSERT_TRUE(ctl.set_strategy("surface.below", InputStrategy::Interactive));
    ASSERT_TRUE(ctl.set_strategy("surface.above", InputStrategy::ClickThrough));

    InputEvent ev;
    ev.target = "surface.above";
    auto routed = ctl.route({ev});

    ASSERT_EQ(routed.size(), 1u);
    EXPECT_EQ(routed[0].decision, RouteDecision::Deliver);
    EXPECT_EQ(routed[0].delivered_to, "surface.below");  // 穿透到底層
}

// --- 路由：Inert 吞掉、不下傳 ---
TEST(E1_02_Route, InertSwallowsAndDoesNotPassBelow) {
    NullKernelBackend backend;
    make_surface(backend, "surface.below");
    make_surface(backend, "surface.inert");
    InputStrategyController ctl(backend);
    ASSERT_TRUE(ctl.set_strategy("surface.below", InputStrategy::Interactive));
    ASSERT_TRUE(ctl.set_strategy("surface.inert", InputStrategy::Inert));

    InputEvent ev;
    ev.target = "surface.inert";
    auto routed = ctl.route({ev});

    ASSERT_EQ(routed.size(), 1u);
    EXPECT_EQ(routed[0].decision, RouteDecision::Swallow);
    EXPECT_TRUE(routed[0].delivered_to.empty());  // 未下傳到 below
}

// --- 路由：穿透落出堆疊底 → 無人接收 ---
TEST(E1_02_Route, ClickThroughFallsOffBottom) {
    NullKernelBackend backend;
    make_surface(backend, "surface.only");
    InputStrategyController ctl(backend);
    ASSERT_TRUE(ctl.set_strategy("surface.only", InputStrategy::ClickThrough));

    InputEvent ev;
    ev.target = "surface.only";
    auto routed = ctl.route({ev});

    ASSERT_EQ(routed.size(), 1u);
    EXPECT_EQ(routed[0].decision, RouteDecision::PassBelow);
    EXPECT_TRUE(routed[0].delivered_to.empty());
}

// --- 路由：未知 / 空 target → PassBelow、無遞送 ---
TEST(E1_02_Route, UnknownTargetPassesBelowEmpty) {
    NullKernelBackend backend;
    make_surface(backend, "surface.known");
    InputStrategyController ctl(backend);
    ASSERT_TRUE(ctl.set_strategy("surface.known", InputStrategy::Interactive));

    InputEvent ev;
    ev.target = "surface.stranger";
    auto routed = ctl.route({ev});

    ASSERT_EQ(routed.size(), 1u);
    EXPECT_EQ(routed[0].decision, RouteDecision::PassBelow);
    EXPECT_TRUE(routed[0].delivered_to.empty());
}

// --- 路由：Capture 全域獨占、無視命中目標 ---
TEST(E1_02_Route, CaptureOverridesTargetGlobally) {
    NullKernelBackend backend(caps_with_capture());
    make_surface(backend, "surface.bg");
    make_surface(backend, "surface.modal");
    InputStrategyController ctl(backend);
    ASSERT_TRUE(ctl.set_strategy("surface.bg", InputStrategy::Interactive));
    ASSERT_TRUE(ctl.set_strategy("surface.modal", InputStrategy::Capture));

    EXPECT_TRUE(ctl.capture_active());
    SurfaceId who;
    EXPECT_TRUE(ctl.capture_active(&who));
    EXPECT_EQ(who, "surface.modal");

    // 事件本欲落在 bg，仍被 modal 全域捕捉。
    InputEvent ev;
    ev.target = "surface.bg";
    auto routed = ctl.route({ev});

    ASSERT_EQ(routed.size(), 1u);
    EXPECT_EQ(routed[0].decision, RouteDecision::CaptureAll);
    EXPECT_EQ(routed[0].delivered_to, "surface.modal");
}

TEST(E1_02_Route, NoCaptureActiveByDefault) {
    NullKernelBackend backend;
    make_surface(backend, "surface.a");
    InputStrategyController ctl(backend);
    ASSERT_TRUE(ctl.set_strategy("surface.a", InputStrategy::Interactive));
    EXPECT_FALSE(ctl.capture_active());
}

// --- 路由：多事件批次 ---
TEST(E1_02_Route, MultipleEventsResolvedIndependently) {
    NullKernelBackend backend;
    make_surface(backend, "surface.low");
    make_surface(backend, "surface.mid");
    InputStrategyController ctl(backend);
    ASSERT_TRUE(ctl.set_strategy("surface.low", InputStrategy::Interactive));
    ASSERT_TRUE(ctl.set_strategy("surface.mid", InputStrategy::ClickThrough));

    InputEvent e1;
    e1.target = "surface.mid";  // 穿透到 low
    InputEvent e2;
    e2.target = "surface.low";  // 直接遞送 low
    auto routed = ctl.route({e1, e2});

    ASSERT_EQ(routed.size(), 2u);
    EXPECT_EQ(routed[0].delivered_to, "surface.low");
    EXPECT_EQ(routed[1].delivered_to, "surface.low");
    EXPECT_EQ(routed[1].decision, RouteDecision::Deliver);
}

// --- K3 整合：poll_and_route 走後端 poll_input（null 永遠空）---
TEST(E1_02_Integration, PollAndRouteUsesBackendPollInput) {
    NullKernelBackend backend;
    make_surface(backend, "surface.a");
    InputStrategyController ctl(backend);
    ASSERT_TRUE(ctl.set_strategy("surface.a", InputStrategy::Interactive));

    EXPECT_EQ(backend.poll_input_calls(), 0u);
    auto routed = ctl.poll_and_route();
    EXPECT_TRUE(routed.empty());                  // null 後端無輸入來源
    EXPECT_EQ(backend.poll_input_calls(), 1u);    // 確有經 K3 poll_input
}

// --- 經 KernelBackend* 基底多型使用（後端可替換）---
TEST(E1_02_Integration, WorksThroughBaseInterface) {
    NullKernelBackend concrete;
    KernelBackend& backend = concrete;
    ASSERT_TRUE(backend.create_surface("surface.p", SurfaceProfile{}));
    InputStrategyController ctl(backend);
    ASSERT_TRUE(ctl.set_strategy("surface.p", InputStrategy::ClickThrough));
    EXPECT_EQ(ctl.strategy("surface.p"), InputStrategy::ClickThrough);
    EXPECT_EQ(concrete.surface_profile("surface.p")->input, InputPolicy::PassThrough);
}

}  // namespace
}  // namespace ds::kernel
