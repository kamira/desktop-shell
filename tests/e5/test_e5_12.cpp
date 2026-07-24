// E5-12 全域指標手勢 — 契約測試（gtest）
//
// 驗證 null 後端的能力閘控 + 手勢辨識 / 分派契約（相位 2 真實後端須遵守同一契約）。
// 相位 1：只驗介面 + null（手動注入）行為，不含任何平台分支。
//
// **本單元為能力閘控項（medium），驗收硬性要求「降級路徑」測試**——
// 即 has()==false 時呼叫端如何安全降級：不崩、不誤觸、明確回報不可用。
// 見下方 `GlobalGestureDegradation` 測試群（本檔驗收關鍵）。
#include "global_gesture.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "capability_matrix.hpp"  // E1-21：以能力矩陣驅動可用性

using ds::events::Gesture;
using ds::events::GestureType;
using ds::events::kGestureCapability;
using ds::events::NullGlobalGestures;
using ds::events::SubscriptionId;
using ds::kernel::CapabilityDecl;
using ds::kernel::CapabilityMatrix;

namespace {

Gesture make_gesture(GestureType type, double magnitude = 0.0,
                     std::string detail = "") {
    return Gesture{type, magnitude, std::move(detail)};
}

// ---------------------------------------------------------------------------
// 能力可用（has()==true）時的正常辨識 / 分派契約
// ---------------------------------------------------------------------------

// 能力可用時 has() 為 true，訂閱→注入→收到，type/magnitude/detail 正確傳遞。
TEST(GlobalGesture, AvailableSubscribeThenInjectDelivers) {
    NullGlobalGestures src(/*available=*/true);
    EXPECT_TRUE(src.has());

    std::vector<Gesture> received;
    const SubscriptionId id = src.subscribe(
        GestureType::SwipeLeft,
        [&](const Gesture& g) { received.push_back(g); });

    EXPECT_NE(id, 0u);
    EXPECT_EQ(src.listener_count(), 1u);

    src.inject(make_gesture(GestureType::SwipeLeft, 0.75, "two-finger"));

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].type, GestureType::SwipeLeft);
    EXPECT_DOUBLE_EQ(received[0].magnitude, 0.75);
    EXPECT_EQ(received[0].detail, "two-finger");
}

// 只分派給訂閱了「該手勢類型」的訂閱者：不同類型不被誤觸。
TEST(GlobalGesture, DispatchOnlyToMatchingGestureType) {
    NullGlobalGestures src(true);
    int swipe_left = 0, corner_tl = 0;
    src.subscribe(GestureType::SwipeLeft, [&](const Gesture&) { ++swipe_left; });
    src.subscribe(GestureType::CornerTopLeft, [&](const Gesture&) { ++corner_tl; });

    src.inject(make_gesture(GestureType::SwipeLeft));
    EXPECT_EQ(swipe_left, 1);
    EXPECT_EQ(corner_tl, 0);  // 不同手勢類型，不被誤觸

    src.inject(make_gesture(GestureType::CornerTopLeft));
    EXPECT_EQ(swipe_left, 1);
    EXPECT_EQ(corner_tl, 1);
}

// 同一手勢類型的多訂閱者皆收，各收一次。
TEST(GlobalGesture, MultipleSubscribersSameGestureAllReceive) {
    NullGlobalGestures src(true);
    int a = 0, b = 0, c = 0;
    src.subscribe(GestureType::PinchOut, [&](const Gesture&) { ++a; });
    src.subscribe(GestureType::PinchOut, [&](const Gesture&) { ++b; });
    src.subscribe(GestureType::PinchOut, [&](const Gesture&) { ++c; });
    EXPECT_EQ(src.listener_count(), 3u);

    src.inject(make_gesture(GestureType::PinchOut, 1.5));
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(c, 1);
}

// 解除訂閱後不再收；其他訂閱者不受影響。
TEST(GlobalGesture, UnsubscribeStopsDelivery) {
    NullGlobalGestures src(true);
    int kept = 0, dropped = 0;
    src.subscribe(GestureType::SwipeUp, [&](const Gesture&) { ++kept; });
    const SubscriptionId drop_id =
        src.subscribe(GestureType::SwipeUp, [&](const Gesture&) { ++dropped; });

    EXPECT_TRUE(src.unsubscribe(drop_id));
    EXPECT_EQ(src.listener_count(), 1u);

    src.inject(make_gesture(GestureType::SwipeUp));
    EXPECT_EQ(kept, 1);
    EXPECT_EQ(dropped, 0);
}

// 未知 id 解除為 no-op：不影響現有訂閱，回傳 false。
TEST(GlobalGesture, UnsubscribeUnknownIdIsNoOp) {
    NullGlobalGestures src(true);
    int hits = 0;
    src.subscribe(GestureType::SwipeDown, [&](const Gesture&) { ++hits; });

    EXPECT_FALSE(src.unsubscribe(0));
    EXPECT_FALSE(src.unsubscribe(999999));
    EXPECT_EQ(src.listener_count(), 1u);

    src.inject(make_gesture(GestureType::SwipeDown));
    EXPECT_EQ(hits, 1);
}

// 空 listener 為無效訂閱：回傳 0、不佔用訂閱槽（即使能力可用）。
TEST(GlobalGesture, EmptyListenerRejectedWhenAvailable) {
    NullGlobalGestures src(true);
    const SubscriptionId id = src.subscribe(GestureType::SwipeRight, nullptr);
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(src.listener_count(), 0u);
}

// 分派順序穩定：依訂閱順序（SubscriptionId 遞增）分派。
TEST(GlobalGesture, DispatchOrderFollowsSubscription) {
    NullGlobalGestures src(true);
    std::vector<int> order;
    src.subscribe(GestureType::CornerBottomRight, [&](const Gesture&) { order.push_back(1); });
    src.subscribe(GestureType::CornerBottomRight, [&](const Gesture&) { order.push_back(2); });
    src.subscribe(GestureType::CornerBottomRight, [&](const Gesture&) { order.push_back(3); });

    src.inject(make_gesture(GestureType::CornerBottomRight));

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

// listener 於回呼中解除自己：不影響本輪分派（快照語意），下一輪不再收。
TEST(GlobalGesture, ListenerUnsubscribingDuringDispatchIsSafe) {
    NullGlobalGestures src(true);
    int self_hits = 0, other_hits = 0;
    SubscriptionId self_id = 0;
    self_id = src.subscribe(GestureType::SwipeLeft, [&](const Gesture&) {
        ++self_hits;
        src.unsubscribe(self_id);
    });
    src.subscribe(GestureType::SwipeLeft, [&](const Gesture&) { ++other_hits; });

    src.inject(make_gesture(GestureType::SwipeLeft));  // 本輪：兩者皆收
    src.inject(make_gesture(GestureType::SwipeLeft));  // 下一輪：self 已解除

    EXPECT_EQ(self_hits, 1);
    EXPECT_EQ(other_hits, 2);
}

// 各具名手勢型別皆能承載並正確分派（各訂各自類型）。
TEST(GlobalGesture, AllGestureTypesDispatch) {
    NullGlobalGestures src(true);
    const std::vector<GestureType> kinds = {
        GestureType::SwipeLeft,        GestureType::SwipeRight,
        GestureType::SwipeUp,          GestureType::SwipeDown,
        GestureType::CornerTopLeft,    GestureType::CornerTopRight,
        GestureType::CornerBottomLeft, GestureType::CornerBottomRight,
        GestureType::PinchIn,          GestureType::PinchOut,
    };
    std::vector<GestureType> seen;
    for (auto k : kinds) {
        src.subscribe(k, [&, k](const Gesture& g) {
            EXPECT_EQ(g.type, k);
            seen.push_back(g.type);
        });
    }
    for (auto k : kinds) {
        src.inject(make_gesture(k));
    }
    ASSERT_EQ(seen.size(), kinds.size());
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        EXPECT_EQ(seen[i], kinds[i]);
    }
}

// ---------------------------------------------------------------------------
// 能力閘控 + 能力矩陣（E1-21）整合
// ---------------------------------------------------------------------------

// from_capability：能力矩陣宣告可用 → has()==true。
TEST(GlobalGesture, FromCapabilityAvailableWhenMatrixSaysSo) {
    CapabilityMatrix matrix({
        {kGestureCapability, "全域指標手勢辨識", /*optional=*/true, /*default_available=*/true},
    });
    NullGlobalGestures src = NullGlobalGestures::from_capability(matrix);
    EXPECT_TRUE(src.has());
}

// from_capability：能力矩陣宣告不可用 → has()==false。
TEST(GlobalGesture, FromCapabilityUnavailableWhenMatrixSaysSo) {
    CapabilityMatrix matrix({
        {kGestureCapability, "全域指標手勢辨識", /*optional=*/true, /*default_available=*/false},
    });
    NullGlobalGestures src = NullGlobalGestures::from_capability(matrix);
    EXPECT_FALSE(src.has());
}

// from_capability：能力未宣告於矩陣（如相位 1 預設矩陣）→ 保守回 false。
// 這正是相位 1 的實況：input.gesture 未在 E1-21 defaults() 宣告，故不可用。
TEST(GlobalGesture, FromCapabilityUndeclaredIsUnavailable) {
    NullGlobalGestures src =
        NullGlobalGestures::from_capability(CapabilityMatrix::defaults());
    EXPECT_FALSE(src.has());  // 未宣告 → 保守不可用
}

// ---------------------------------------------------------------------------
// **降級路徑（medium 驗收硬性要求）**：has()==false 時呼叫端如何安全降級。
// 契約：不崩、不誤觸、明確回報不可用。
// ---------------------------------------------------------------------------

// 降級 1：能力不可用時 has() 明確回報 false（呼叫端據此走降級路徑）。
TEST(GlobalGestureDegradation, HasReportsUnavailableClearly) {
    NullGlobalGestures src(/*available=*/false);
    EXPECT_FALSE(src.has());  // 明確、可查詢的不可用信號
}

// 降級 2：能力不可用時 subscribe 被拒、回 0，且不佔用訂閱槽。
// 呼叫端由「回 0」即知能力不可用，可安全改走 fallback。
TEST(GlobalGestureDegradation, SubscribeRejectedWhenUnavailable) {
    NullGlobalGestures src(false);
    bool fired = false;
    const SubscriptionId id = src.subscribe(
        GestureType::SwipeLeft, [&](const Gesture&) { fired = true; });

    EXPECT_EQ(id, 0u);                  // 明確回報不可用（訂閱被拒）
    EXPECT_EQ(src.listener_count(), 0u);  // 未建立任何假訂閱
    EXPECT_FALSE(fired);
}

// 降級 3（不誤觸）：能力不可用時注入為 no-op——即使呼叫端漏查 has() 而「訂閱」，
// 也絕不會有任何回呼被觸發。這是「不誤觸」的核心保證。
TEST(GlobalGestureDegradation, InjectNeverMisfiresWhenUnavailable) {
    NullGlobalGestures src(false);
    int misfires = 0;
    // 呼叫端未先查 has() 便嘗試訂閱（模擬有瑕疵的呼叫端）——訂閱其實被拒。
    src.subscribe(GestureType::CornerTopRight, [&](const Gesture&) { ++misfires; });

    // 注入各種手勢，全部應為 no-op、不誤觸。
    src.inject(make_gesture(GestureType::CornerTopRight));
    src.inject(make_gesture(GestureType::SwipeUp, 1.0, "phantom"));

    EXPECT_EQ(misfires, 0);  // 絕不誤觸
    EXPECT_EQ(src.listener_count(), 0u);
}

// 降級 4（不崩）：能力不可用時，對介面的所有操作皆安全、不崩潰、不拋例外。
TEST(GlobalGestureDegradation, AllOperationsSafeWhenUnavailable) {
    NullGlobalGestures src(false);
    EXPECT_NO_THROW({
        (void)src.has();
        (void)src.subscribe(GestureType::PinchIn, [](const Gesture&) {});
        (void)src.subscribe(GestureType::PinchIn, nullptr);
        (void)src.unsubscribe(0);
        (void)src.unsubscribe(12345);
        (void)src.listener_count();
        src.inject(make_gesture(GestureType::PinchIn));
    });
    EXPECT_EQ(src.listener_count(), 0u);
}

// 降級 5（正規呼叫端範式）：以 has() 閘控選擇主路徑 / 降級路徑。
// 這演示驗收要求的「呼叫端如何安全降級」——不可用時走 fallback，明確回報且不誤觸。
TEST(GlobalGestureDegradation, GatedCallerFallsBackCleanly) {
    // 一個依 has() 閘控的呼叫端輔助：可用則訂閱手勢，不可用則走鍵盤 fallback。
    auto wire_feature = [](NullGlobalGestures& gestures, bool& gesture_path,
                           bool& fallback_path) {
        if (gestures.has()) {
            const SubscriptionId id = gestures.subscribe(
                GestureType::SwipeLeft, [&](const Gesture&) {});
            gesture_path = (id != 0);
        } else {
            // 降級路徑：能力不可用，改用不需手勢的替代方案（如鍵盤快捷鍵）。
            fallback_path = true;
        }
    };

    // 能力可用 → 走手勢主路徑。
    {
        NullGlobalGestures available(true);
        bool gesture_path = false, fallback_path = false;
        wire_feature(available, gesture_path, fallback_path);
        EXPECT_TRUE(gesture_path);
        EXPECT_FALSE(fallback_path);
    }
    // 能力不可用 → 乾淨降級到 fallback，不建立手勢訂閱、不誤觸。
    {
        NullGlobalGestures unavailable(false);
        bool gesture_path = false, fallback_path = false;
        wire_feature(unavailable, gesture_path, fallback_path);
        EXPECT_FALSE(gesture_path);
        EXPECT_TRUE(fallback_path);
        EXPECT_EQ(unavailable.listener_count(), 0u);
    }
}

// 降級 6（矩陣驅動的端到端降級）：由 E1-21 能力矩陣判定不可用，呼叫端據此降級。
TEST(GlobalGestureDegradation, MatrixDrivenUnavailableTriggersFallback) {
    // 相位 1 預設矩陣未宣告 input.gesture → 不可用。
    NullGlobalGestures src =
        NullGlobalGestures::from_capability(CapabilityMatrix::defaults());
    ASSERT_FALSE(src.has());

    bool used_fallback = false;
    if (!src.has()) {
        used_fallback = true;  // 明確回報不可用 → 降級
    }
    EXPECT_TRUE(used_fallback);

    // 即便仍嘗試訂閱與注入，也不誤觸。
    int hits = 0;
    src.subscribe(GestureType::SwipeRight, [&](const Gesture&) { ++hits; });
    src.inject(make_gesture(GestureType::SwipeRight));
    EXPECT_EQ(hits, 0);
}

}  // namespace
