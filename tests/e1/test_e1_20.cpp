// E1-20 睡眠喚醒復原 — 契約測試（gtest）
//
// 驗證：
//   - 初始狀態：未經歷任何 SystemSleep 前無快照、計數為 0。
//   - 睡眠事件保存狀態：SystemSleep 保存目前 LayerStack 的具名圖層指派 + 可見性快照。
//   - 喚醒事件還原：SystemWake 依快照以 LayerStack::assign() 重建圖層指派。
//   - 圖層可見性復原：on_wake 回呼帶回睡眠當下的可見性快照（非喚醒當下的即時查詢）。
//   - 非睡眠 / 喚醒事件忽略：DisplayChanged / SessionLocked 等不影響快照與計數。
//   - 經 E5-08 事件驅動：建構即訂閱（listener_count 增加），事件全經 inject() 注入。
//   - 訂閱解除：解構後 listener_count 歸零。
//   - 多次睡眠喚醒循環：每輪 SystemSleep 皆是全新快照，sleep_count / wake_count 各自累計。
//   - 喚醒無先前睡眠：保守 no-op，不崩潰。
//   - 失效資源處理：ResourceValidator 判定失效者不重建指派，改觸發 on_invalidated。
//   - NFR-03：能力閘控（kernel.surface 不可用）時，指派保守略過、has() 代理正確、不崩潰。
// 相位 1：來源與 LayerStack 皆為注入式，不含任何平台分支（無 #ifdef / win32 / cocoa /
//   IOKit）。NFR-02：本測試不出現任何數字 z-order / z-index / 絕對座標。
#include "sleep_wake_recovery.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::events::NullSystemEventSource;
using ds::events::SystemEvent;
using ds::events::SystemEventType;
using ds::kernel::CapabilityDecl;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerAssign;
using ds::kernel::LayerStack;
using ds::kernel::SleepWakeRecovery;
using ds::kernel::SurfaceId;
using ds::kernel::SurfaceLayer;

namespace {

// 建一個「kernel.surface 不可用」的能力矩陣，供 NFR-03 閘控（拒絕）路徑測試。
CapabilityMatrix without_surface_capability() {
    return CapabilityMatrix(std::vector<CapabilityDecl>{
        {"kernel.surface", "surface 核心（測試設為不可用）",
         /*optional=*/false, /*default_available=*/false}});
}

}  // namespace

TEST(SleepWakeRecovery, InitiallyNoSnapshot) {
    NullSystemEventSource source;
    LayerStack stack;
    SleepWakeRecovery recovery(source, stack);

    EXPECT_FALSE(recovery.has_snapshot());
    EXPECT_EQ(recovery.snapshot_count(), 0u);
    EXPECT_EQ(recovery.sleep_count(), 0u);
    EXPECT_EQ(recovery.wake_count(), 0u);
}

TEST(SleepWakeRecovery, SleepSavesLayerAssignments) {
    NullSystemEventSource source;
    LayerStack stack;
    ASSERT_EQ(stack.assign("surface.wallpaper", SurfaceLayer::Wallpaper), LayerAssign::Ok);
    ASSERT_EQ(stack.assign("surface.panel", SurfaceLayer::Overlay), LayerAssign::Ok);

    SleepWakeRecovery recovery(source, stack);
    source.inject(SystemEvent{SystemEventType::SystemSleep, ""});

    EXPECT_TRUE(recovery.has_snapshot());
    EXPECT_EQ(recovery.snapshot_count(), 2u);
    EXPECT_EQ(recovery.sleep_count(), 1u);
    ASSERT_TRUE(recovery.snapshot_contains("surface.wallpaper"));
    ASSERT_TRUE(recovery.snapshot_contains("surface.panel"));
    ASSERT_NE(recovery.snapshot_layer_of("surface.wallpaper"), nullptr);
    EXPECT_EQ(*recovery.snapshot_layer_of("surface.wallpaper"), SurfaceLayer::Wallpaper);
    ASSERT_NE(recovery.snapshot_layer_of("surface.panel"), nullptr);
    EXPECT_EQ(*recovery.snapshot_layer_of("surface.panel"), SurfaceLayer::Overlay);
}

TEST(SleepWakeRecovery, WakeRestoresLayerAssignments) {
    NullSystemEventSource source;
    LayerStack stack;
    ASSERT_EQ(stack.assign("surface.panel", SurfaceLayer::Overlay), LayerAssign::Ok);

    SleepWakeRecovery recovery(source, stack);
    source.inject(SystemEvent{SystemEventType::SystemSleep, ""});

    // 模擬睡眠期間指派遺失（如後端資源重置）。
    ASSERT_TRUE(stack.remove("surface.panel"));
    ASSERT_FALSE(stack.contains("surface.panel"));

    source.inject(SystemEvent{SystemEventType::SystemWake, ""});

    EXPECT_TRUE(stack.contains("surface.panel"));
    ASSERT_NE(stack.layer_of("surface.panel"), nullptr);
    EXPECT_EQ(*stack.layer_of("surface.panel"), SurfaceLayer::Overlay);
    EXPECT_EQ(recovery.wake_count(), 1u);
}

TEST(SleepWakeRecovery, VisibilityRecoveredViaCallbackReflectsSleepTimeSnapshot) {
    NullSystemEventSource source;
    LayerStack stack;
    ASSERT_EQ(stack.assign("surface.a", SurfaceLayer::Normal), LayerAssign::Ok);
    ASSERT_EQ(stack.assign("surface.b", SurfaceLayer::Normal), LayerAssign::Ok);

    // 外部可變狀態：睡眠當下 a 可見、b 不可見。
    bool a_visible = true;
    bool b_visible = false;
    auto visibility = [&](const SurfaceId& id) -> bool {
        if (id == "surface.a") return a_visible;
        if (id == "surface.b") return b_visible;
        return true;
    };

    SleepWakeRecovery recovery(source, stack, visibility);
    source.inject(SystemEvent{SystemEventType::SystemSleep, ""});

    // 睡眠後、喚醒前，外部可見性狀態改變——喚醒還原應反映「睡眠當下」快照，而非即時查詢。
    a_visible = false;
    b_visible = true;

    std::vector<std::pair<SurfaceId, bool>> restored;
    recovery.set_on_wake([&](const SurfaceId& id, SurfaceLayer, bool visible) {
        restored.emplace_back(id, visible);
    });
    source.inject(SystemEvent{SystemEventType::SystemWake, ""});

    ASSERT_EQ(restored.size(), 2u);
    for (const auto& [id, visible] : restored) {
        if (id == "surface.a") {
            EXPECT_TRUE(visible);   // 睡眠當下 a 為可見
        } else if (id == "surface.b") {
            EXPECT_FALSE(visible);  // 睡眠當下 b 為不可見
        } else {
            EXPECT_TRUE(false) << "unexpected surface id in restore callback: " << id;
        }
    }
}

TEST(SleepWakeRecovery, NoVisibilityProviderDefaultsToVisible) {
    NullSystemEventSource source;
    LayerStack stack;
    ASSERT_EQ(stack.assign("surface.only", SurfaceLayer::Normal), LayerAssign::Ok);

    SleepWakeRecovery recovery(source, stack);  // 無 VisibilityProvider
    source.inject(SystemEvent{SystemEventType::SystemSleep, ""});

    ASSERT_NE(recovery.snapshot_visible_of("surface.only"), nullptr);
    EXPECT_TRUE(*recovery.snapshot_visible_of("surface.only"));
}

TEST(SleepWakeRecovery, NonSleepWakeEventsIgnored) {
    NullSystemEventSource source;
    LayerStack stack;
    ASSERT_EQ(stack.assign("surface.panel", SurfaceLayer::Overlay), LayerAssign::Ok);

    SleepWakeRecovery recovery(source, stack);

    source.inject(SystemEvent{SystemEventType::DisplayChanged, ""});
    source.inject(SystemEvent{SystemEventType::SessionLocked, ""});
    source.inject(SystemEvent{SystemEventType::SessionUnlocked, ""});
    source.inject(SystemEvent{SystemEventType::PowerStatusChanged, ""});

    EXPECT_FALSE(recovery.has_snapshot());
    EXPECT_EQ(recovery.sleep_count(), 0u);
    EXPECT_EQ(recovery.wake_count(), 0u);
}

TEST(SleepWakeRecovery, EventDrivenViaE508Subscription) {
    NullSystemEventSource source;
    LayerStack stack;
    EXPECT_EQ(source.listener_count(), 0u);
    {
        SleepWakeRecovery recovery(source, stack);
        EXPECT_EQ(source.listener_count(), 1u);
        (void)recovery;
    }
    EXPECT_EQ(source.listener_count(), 0u);
}

TEST(SleepWakeRecovery, UnsubscribesOnDestruction) {
    NullSystemEventSource source;
    LayerStack stack;
    {
        SleepWakeRecovery recovery(source, stack);
        ASSERT_EQ(source.listener_count(), 1u);
    }
    // 解構後訂閱應已解除；再注入事件不應有任何殘留監聽者接手（無崩潰、無殘留訂閱）。
    EXPECT_EQ(source.listener_count(), 0u);
    source.inject(SystemEvent{SystemEventType::SystemSleep, ""});  // 不應崩潰
}

TEST(SleepWakeRecovery, MultipleSleepWakeCycles) {
    NullSystemEventSource source;
    LayerStack stack;
    ASSERT_EQ(stack.assign("surface.x", SurfaceLayer::Normal), LayerAssign::Ok);

    SleepWakeRecovery recovery(source, stack);

    // 第一輪：x 在 Normal。
    source.inject(SystemEvent{SystemEventType::SystemSleep, ""});
    EXPECT_EQ(recovery.sleep_count(), 1u);
    ASSERT_NE(recovery.snapshot_layer_of("surface.x"), nullptr);
    EXPECT_EQ(*recovery.snapshot_layer_of("surface.x"), SurfaceLayer::Normal);
    source.inject(SystemEvent{SystemEventType::SystemWake, ""});
    EXPECT_EQ(recovery.wake_count(), 1u);

    // 第二輪：改派 x 到 Overlay 後再睡眠——快照應為全新（取代前一輪）。
    ASSERT_EQ(stack.assign("surface.x", SurfaceLayer::Overlay), LayerAssign::Moved);
    source.inject(SystemEvent{SystemEventType::SystemSleep, ""});
    EXPECT_EQ(recovery.sleep_count(), 2u);
    ASSERT_NE(recovery.snapshot_layer_of("surface.x"), nullptr);
    EXPECT_EQ(*recovery.snapshot_layer_of("surface.x"), SurfaceLayer::Overlay);

    ASSERT_TRUE(stack.remove("surface.x"));
    source.inject(SystemEvent{SystemEventType::SystemWake, ""});
    EXPECT_EQ(recovery.wake_count(), 2u);
    ASSERT_NE(stack.layer_of("surface.x"), nullptr);
    EXPECT_EQ(*stack.layer_of("surface.x"), SurfaceLayer::Overlay);
}

TEST(SleepWakeRecovery, WakeWithoutPriorSleepIsConservativeNoOp) {
    NullSystemEventSource source;
    LayerStack stack;
    SleepWakeRecovery recovery(source, stack);

    bool wake_called = false;
    recovery.set_on_wake([&](const SurfaceId&, SurfaceLayer, bool) { wake_called = true; });

    source.inject(SystemEvent{SystemEventType::SystemWake, ""});  // 未曾睡眠

    EXPECT_EQ(recovery.wake_count(), 1u);  // 計數仍累加（喚醒事件確實發生過）
    EXPECT_FALSE(recovery.has_snapshot());
    EXPECT_FALSE(wake_called);  // 但無快照可還原，回呼不觸發
}

TEST(SleepWakeRecovery, InvalidatedResourceSkipsRestoreAndNotifies) {
    NullSystemEventSource source;
    LayerStack stack;
    ASSERT_EQ(stack.assign("surface.gone", SurfaceLayer::Normal), LayerAssign::Ok);
    ASSERT_EQ(stack.assign("surface.stays", SurfaceLayer::Overlay), LayerAssign::Ok);

    SleepWakeRecovery recovery(source, stack);
    source.inject(SystemEvent{SystemEventType::SystemSleep, ""});

    // 睡眠期間兩者皆遺失（模擬後端重置）。
    ASSERT_TRUE(stack.remove("surface.gone"));
    ASSERT_TRUE(stack.remove("surface.stays"));

    recovery.set_resource_validator(
        [](const SurfaceId& id) { return id != "surface.gone"; });

    std::vector<SurfaceId> invalidated;
    recovery.set_on_invalidated([&](const SurfaceId& id) { invalidated.push_back(id); });
    std::vector<SurfaceId> restored;
    recovery.set_on_wake(
        [&](const SurfaceId& id, SurfaceLayer, bool) { restored.push_back(id); });

    source.inject(SystemEvent{SystemEventType::SystemWake, ""});

    EXPECT_FALSE(stack.contains("surface.gone"));   // 失效：不重建指派
    EXPECT_TRUE(stack.contains("surface.stays"));   // 有效：正常重建
    ASSERT_EQ(invalidated.size(), 1u);
    EXPECT_EQ(invalidated[0], "surface.gone");
    ASSERT_EQ(restored.size(), 1u);
    EXPECT_EQ(restored[0], "surface.stays");
}

TEST(SleepWakeRecovery, CapabilityGatedRestoreIsConservativeAndProxiesHas) {
    LayerStack stack(without_surface_capability());
    NullSystemEventSource source;
    SleepWakeRecovery recovery(source, stack);

    EXPECT_FALSE(recovery.has("kernel.surface"));  // NFR-03：has() 正確代理至 LayerStack

    // 能力不可用：assign 一律拒絕，堆疊維持空。
    EXPECT_EQ(stack.assign("surface.a", SurfaceLayer::Normal),
              LayerAssign::RejectedNoCapability);
    EXPECT_FALSE(stack.contains("surface.a"));

    source.inject(SystemEvent{SystemEventType::SystemSleep, ""});
    EXPECT_TRUE(recovery.has_snapshot());
    EXPECT_EQ(recovery.snapshot_count(), 0u);  // 從未成功指派，快照為空。

    bool wake_called = false;
    recovery.set_on_wake([&](const SurfaceId&, SurfaceLayer, bool) { wake_called = true; });
    source.inject(SystemEvent{SystemEventType::SystemWake, ""});  // 不應崩潰

    EXPECT_FALSE(wake_called);
    EXPECT_EQ(recovery.wake_count(), 1u);
}
