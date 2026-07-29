// E1-23 分層失敗的手動降級路徑 — 契約 / 單元測試（gtest）
//
// 驗證：
//   - 分層能力可用、assign 成功 → try_layered 正常路徑，不降級
//   - 分層能力不可用 → 自動降級扁平化（原因 CapabilityUnavailable，可見回報）
//   - assign 失敗（非能力問題，如空 id）→ 同樣觸發降級（原因 AssignRejected）
//   - 降級原因可見回報：LayerFallbackResult.reason / degrade_reason() / last_degraded()
//   - fallback_flatten() 可直接手動呼叫（不經 try_layered 的自動判定）
//   - 可注入降級策略：自訂 layered_available() / flatten_target()
//   - 多層降級：多個不同請求圖層在降級時全部扁平化到同一具名圖層（無 overlay 差異化）
//   - 能力恢復後（換到能力可用的 stack）回到正常分層路徑
// 相位 1：不含任何平台分支（無 #ifdef / win32 / cocoa）。
// NFR-02：本測試不出現任何數字 z-order / z-index；一律以具名圖層 / 具名關係斷言。
#include "layer_fallback.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using ds::kernel::CapabilityDecl;
using ds::kernel::CapabilityMatrix;
using ds::kernel::FallbackReason;
using ds::kernel::FallbackStrategy;
using ds::kernel::LayerAssign;
using ds::kernel::LayerFallback;
using ds::kernel::LayerFallbackResult;
using ds::kernel::LayerStack;
using ds::kernel::SurfaceLayer;

namespace {

// 建一個「kernel.surface 不可用」的能力矩陣（與 test_e1_01.cpp 同慣例），供分層能力
// 不可用的降級路徑測試。
CapabilityMatrix without_surface_capability() {
    return CapabilityMatrix(std::vector<CapabilityDecl>{
        {"kernel.surface", "surface 核心（測試設為不可用）",
         /*optional=*/false, /*default_available=*/false},
    });
}

// 自訂降級策略：不論底層 stack 實際能力為何，一律視為「分層不可用」→ 強制走降級路徑。
// 供「可注入降級策略」與「多層降級」測試：即使底層能力其實可用（assign 因此仍會成功），
// 策略仍可主動要求降級，展示呼叫端可客製降級判斷。
class AlwaysDegradeStrategy : public FallbackStrategy {
public:
    bool layered_available(const LayerStack&) const override { return false; }
};

// 自訂降級策略：強制降級，且扁平化目的層改為 BelowNormal（而非預設 Normal）。
class ForceToBelowNormalStrategy : public FallbackStrategy {
public:
    bool layered_available(const LayerStack&) const override { return false; }
    SurfaceLayer flatten_target() const override { return SurfaceLayer::BelowNormal; }
};

// --- 分層可用 → 正常路徑 ---

TEST(TryLayered, CapabilityAvailableAssignSucceedsNoDegrade) {
    LayerStack stack;  // 預設能力矩陣：kernel.surface 可用
    LayerFallback fb;
    const LayerFallbackResult result = fb.try_layered(stack, "surface.hud", SurfaceLayer::Overlay);
    EXPECT_FALSE(result.degraded);
    EXPECT_EQ(result.reason, FallbackReason::None);
    EXPECT_EQ(result.assign_result, LayerAssign::Ok);
    EXPECT_EQ(result.effective_layer, SurfaceLayer::Overlay);
    ASSERT_NE(stack.layer_of("surface.hud"), nullptr);
    EXPECT_EQ(*stack.layer_of("surface.hud"), SurfaceLayer::Overlay);
    EXPECT_FALSE(fb.last_degraded());
    EXPECT_EQ(fb.degrade_reason(), FallbackReason::None);
}

// --- 能力不可用 → 降級扁平化 ---

TEST(TryLayered, CapabilityUnavailableDegradesToFlatten) {
    LayerStack stack(without_surface_capability());
    LayerFallback fb;
    const LayerFallbackResult result =
        fb.try_layered(stack, "surface.panel", SurfaceLayer::Overlay);
    EXPECT_TRUE(result.degraded);
    EXPECT_EQ(result.reason, FallbackReason::CapabilityUnavailable);
    EXPECT_EQ(result.effective_layer, SurfaceLayer::Normal);  // 單層扁平化，非請求的 Overlay
    // 底層能力矩陣本身完全不可用，連降級扁平化的 assign 也一併被拒（NFR-03：不改任何狀態）；
    // 但降級「有沒有發生、為什麼」仍透過結果可見回報，不因寫入失敗而被靜默吞掉。
    EXPECT_EQ(result.assign_result, LayerAssign::RejectedNoCapability);
    EXPECT_FALSE(stack.contains("surface.panel"));
    EXPECT_TRUE(fb.last_degraded());
    EXPECT_EQ(fb.degrade_reason(), FallbackReason::CapabilityUnavailable);
}

// --- assign 失敗（非能力問題）→ 降級 ---

TEST(TryLayered, AssignRejectedNonCapabilityStillDegrades) {
    LayerStack stack;  // 能力可用；失敗原因非能力問題，而是空 id
    LayerFallback fb;
    const LayerFallbackResult result = fb.try_layered(stack, "", SurfaceLayer::Overlay);
    EXPECT_TRUE(result.degraded);
    EXPECT_EQ(result.reason, FallbackReason::AssignRejected);
    EXPECT_EQ(result.assign_result, LayerAssign::RejectedEmptyId);
    EXPECT_EQ(stack.size(), 0u);
    EXPECT_EQ(fb.degrade_reason(), FallbackReason::AssignRejected);
}

// --- 降級原因可見回報 ---

TEST(DegradeVisibility, InitialStateIsNotDegraded) {
    LayerFallback fb;
    EXPECT_FALSE(fb.last_degraded());
    EXPECT_EQ(fb.degrade_reason(), FallbackReason::None);
}

TEST(DegradeVisibility, ReasonReflectsMostRecentCall) {
    LayerStack unavailable(without_surface_capability());
    LayerStack available;  // 能力可用
    LayerFallback fb;

    fb.try_layered(unavailable, "x", SurfaceLayer::Overlay);
    EXPECT_TRUE(fb.last_degraded());
    EXPECT_EQ(fb.degrade_reason(), FallbackReason::CapabilityUnavailable);

    // 換一個能力可用的 stack 呼叫 → 回到正常，degrade 狀態同步更新（不殘留舊原因）。
    fb.try_layered(available, "y", SurfaceLayer::Overlay);
    EXPECT_FALSE(fb.last_degraded());
    EXPECT_EQ(fb.degrade_reason(), FallbackReason::None);
}

// --- fallback_flatten()：直接手動降級 ---

TEST(FallbackFlatten, DirectManualFlattenWithCapabilityAvailableReportsStrategyForced) {
    LayerStack stack;  // 能力可用，但呼叫端仍主動要求降級
    LayerFallback fb;
    const LayerFallbackResult result = fb.fallback_flatten(stack, "surface.manual");
    EXPECT_TRUE(result.degraded);
    EXPECT_EQ(result.reason, FallbackReason::StrategyForced);
    EXPECT_EQ(result.assign_result, LayerAssign::Ok);
    EXPECT_EQ(result.effective_layer, SurfaceLayer::Normal);
    ASSERT_NE(stack.layer_of("surface.manual"), nullptr);
    EXPECT_EQ(*stack.layer_of("surface.manual"), SurfaceLayer::Normal);
}

TEST(FallbackFlatten, DirectManualFlattenWithCapabilityUnavailableReportsCapabilityReason) {
    LayerStack stack(without_surface_capability());
    LayerFallback fb;
    const LayerFallbackResult result = fb.fallback_flatten(stack, "surface.manual");
    EXPECT_TRUE(result.degraded);
    EXPECT_EQ(result.reason, FallbackReason::CapabilityUnavailable);
    EXPECT_EQ(result.assign_result, LayerAssign::RejectedNoCapability);
}

// --- 可注入降級策略 ---

TEST(InjectableStrategy, CustomFlattenTargetIsHonored) {
    LayerStack stack;  // 底層能力可用；策略強制降級 + 自訂扁平目的層
    LayerFallback fb(std::make_shared<ForceToBelowNormalStrategy>());
    const LayerFallbackResult result =
        fb.try_layered(stack, "surface.custom", SurfaceLayer::Topmost);
    EXPECT_TRUE(result.degraded);
    EXPECT_EQ(result.effective_layer, SurfaceLayer::BelowNormal);
    EXPECT_EQ(result.assign_result, LayerAssign::Ok);
    ASSERT_NE(stack.layer_of("surface.custom"), nullptr);
    EXPECT_EQ(*stack.layer_of("surface.custom"), SurfaceLayer::BelowNormal);
}

TEST(InjectableStrategy, SetStrategySwapsBehaviorAtRuntime) {
    LayerStack stack;  // 能力可用
    LayerFallback fb;  // 預設策略：能力可用即正常路徑
    const LayerFallbackResult before = fb.try_layered(stack, "a", SurfaceLayer::Overlay);
    EXPECT_FALSE(before.degraded);

    fb.set_strategy(std::make_shared<AlwaysDegradeStrategy>());
    const LayerFallbackResult after = fb.try_layered(stack, "b", SurfaceLayer::Overlay);
    EXPECT_TRUE(after.degraded);
    EXPECT_EQ(after.effective_layer, SurfaceLayer::Normal);
}

TEST(InjectableStrategy, NullStrategyFallsBackToDefaultSafely) {
    LayerFallback fb(nullptr);  // 保守：注入 nullptr 不崩潰，退回內建預設策略
    LayerStack stack;
    const LayerFallbackResult result = fb.try_layered(stack, "surface.safe", SurfaceLayer::Overlay);
    EXPECT_FALSE(result.degraded);
    EXPECT_EQ(result.assign_result, LayerAssign::Ok);
}

// --- 多層降級：多個不同請求圖層全部扁平化到同一具名圖層 ---

TEST(MultiLayerDegrade, ForcedDegradeStrategyFlattensDifferentRequestedLayers) {
    LayerStack stack;  // 底層能力其實可用（assign 本身不會失敗）
    LayerFallback fb(std::make_shared<AlwaysDegradeStrategy>());

    // 刻意請求三個不同的具名圖層 —— 若走正常分層路徑，理應落在三個不同層。
    const LayerFallbackResult r1 = fb.try_layered(stack, "s1", SurfaceLayer::Wallpaper);
    const LayerFallbackResult r2 = fb.try_layered(stack, "s2", SurfaceLayer::Overlay);
    const LayerFallbackResult r3 = fb.try_layered(stack, "s3", SurfaceLayer::Topmost);

    for (const LayerFallbackResult& r : {r1, r2, r3}) {
        EXPECT_TRUE(r.degraded);
        EXPECT_EQ(r.reason, FallbackReason::CapabilityUnavailable);
        EXPECT_EQ(r.effective_layer, SurfaceLayer::Normal);
        EXPECT_EQ(r.assign_result, LayerAssign::Ok);
    }

    // 三個 surface 全部落在同一具名圖層（單層扁平化，不出現 overlay 差異化）。
    ASSERT_NE(stack.layer_of("s1"), nullptr);
    EXPECT_EQ(*stack.layer_of("s1"), SurfaceLayer::Normal);
    ASSERT_NE(stack.layer_of("s2"), nullptr);
    EXPECT_EQ(*stack.layer_of("s2"), SurfaceLayer::Normal);
    ASSERT_NE(stack.layer_of("s3"), nullptr);
    EXPECT_EQ(*stack.layer_of("s3"), SurfaceLayer::Normal);
    EXPECT_EQ(stack.count_in(SurfaceLayer::Normal), 3u);
    EXPECT_EQ(stack.count_in(SurfaceLayer::Overlay), 0u);
    EXPECT_EQ(stack.count_in(SurfaceLayer::Topmost), 0u);
    EXPECT_EQ(stack.count_in(SurfaceLayer::Wallpaper), 0u);
}

// --- 能力恢復後回正常 ---

TEST(CapabilityRecovery, AfterCapabilityRestoredNormalPathResumes) {
    LayerFallback fb;

    // 情境一：能力不可用 → 降級。
    LayerStack degraded_stack(without_surface_capability());
    const LayerFallbackResult degraded_result =
        fb.try_layered(degraded_stack, "surface.panel", SurfaceLayer::Overlay);
    EXPECT_TRUE(degraded_result.degraded);
    EXPECT_EQ(degraded_result.reason, FallbackReason::CapabilityUnavailable);

    // 情境二：能力恢復（同一 surface id，換到能力可用的 stack）→ 回到正常分層路徑，
    // 落在原本請求的具名圖層，不再降級。
    LayerStack restored_stack;  // 預設能力矩陣：kernel.surface 可用
    const LayerFallbackResult restored_result =
        fb.try_layered(restored_stack, "surface.panel", SurfaceLayer::Overlay);
    EXPECT_FALSE(restored_result.degraded);
    EXPECT_EQ(restored_result.reason, FallbackReason::None);
    EXPECT_EQ(restored_result.assign_result, LayerAssign::Ok);
    EXPECT_EQ(restored_result.effective_layer, SurfaceLayer::Overlay);
    ASSERT_NE(restored_stack.layer_of("surface.panel"), nullptr);
    EXPECT_EQ(*restored_stack.layer_of("surface.panel"), SurfaceLayer::Overlay);
    EXPECT_FALSE(fb.last_degraded());
    EXPECT_EQ(fb.degrade_reason(), FallbackReason::None);
}

}  // namespace
