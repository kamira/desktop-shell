// E5-08 系統事件 — 契約測試（gtest）
//
// 驗證 null 後端的分派契約（相位 2 真實後端須遵守同一契約）：
//   訂閱→注入事件→驗證收到；未訂閱不收；多訂閱者皆收；解除訂閱後不再收；
//   未知 id 解除為 no-op；空 listener 為無效訂閱；事件承載正確 type/detail。
// 相位 1：只驗介面 + null（手動注入）行為，不含任何平台分支。
#include "system_event.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::events::NullSystemEventSource;
using ds::events::SubscriptionId;
using ds::events::SystemEvent;
using ds::events::SystemEventType;

namespace {

SystemEvent make_event(SystemEventType type, std::string detail = "") {
    return SystemEvent{type, std::move(detail)};
}

// 訂閱→注入→收到：single listener 收到注入的事件，type 與 detail 正確傳遞。
TEST(SystemEvent, SubscribeThenInjectDelivers) {
    NullSystemEventSource src;
    std::vector<SystemEvent> received;
    const SubscriptionId id = src.subscribe(
        [&](const SystemEvent& e) { received.push_back(e); });

    EXPECT_NE(id, 0u);
    EXPECT_EQ(src.listener_count(), 1u);

    src.inject(make_event(SystemEventType::SystemSleep, "lid closed"));

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].type, SystemEventType::SystemSleep);
    EXPECT_EQ(received[0].detail, "lid closed");
}

// 未訂閱者不收：注入事件時沒有訂閱者，分派為 no-op、不崩潰。
TEST(SystemEvent, NoSubscribersReceivesNothing) {
    NullSystemEventSource src;
    EXPECT_EQ(src.listener_count(), 0u);
    // 無訂閱者時注入不得崩潰。
    src.inject(make_event(SystemEventType::SystemWake));
    EXPECT_EQ(src.listener_count(), 0u);
}

// 多訂閱者皆收：每個訂閱者各收到一次同一事件。
TEST(SystemEvent, MultipleSubscribersAllReceive) {
    NullSystemEventSource src;
    int a = 0, b = 0, c = 0;
    src.subscribe([&](const SystemEvent&) { ++a; });
    src.subscribe([&](const SystemEvent&) { ++b; });
    src.subscribe([&](const SystemEvent&) { ++c; });
    EXPECT_EQ(src.listener_count(), 3u);

    src.inject(make_event(SystemEventType::DisplayChanged, "external attached"));

    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(c, 1);
}

// 解除訂閱後不再收：unsubscribe 後該 listener 不再被分派；其他 listener 不受影響。
TEST(SystemEvent, UnsubscribeStopsDelivery) {
    NullSystemEventSource src;
    int kept = 0, dropped = 0;
    src.subscribe([&](const SystemEvent&) { ++kept; });
    const SubscriptionId drop_id =
        src.subscribe([&](const SystemEvent&) { ++dropped; });

    EXPECT_TRUE(src.unsubscribe(drop_id));
    EXPECT_EQ(src.listener_count(), 1u);

    src.inject(make_event(SystemEventType::SessionLocked));
    EXPECT_EQ(kept, 1);
    EXPECT_EQ(dropped, 0);  // 已解除，不再收
}

// 未知 id 解除為 no-op：不影響現有訂閱，回傳 false。
TEST(SystemEvent, UnsubscribeUnknownIdIsNoOp) {
    NullSystemEventSource src;
    int hits = 0;
    src.subscribe([&](const SystemEvent&) { ++hits; });

    EXPECT_FALSE(src.unsubscribe(0));      // 無效 id
    EXPECT_FALSE(src.unsubscribe(999999));  // 未知 id
    EXPECT_EQ(src.listener_count(), 1u);

    src.inject(make_event(SystemEventType::SessionUnlocked));
    EXPECT_EQ(hits, 1);
}

// 空 listener 為無效訂閱：回傳 0、不佔用訂閱槽。
TEST(SystemEvent, EmptyListenerRejected) {
    NullSystemEventSource src;
    const SubscriptionId id = src.subscribe(nullptr);
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(src.listener_count(), 0u);
}

// 各事件型別皆能承載並正確分派。
TEST(SystemEvent, AllEventTypesDispatch) {
    NullSystemEventSource src;
    std::vector<SystemEventType> seen;
    src.subscribe([&](const SystemEvent& e) { seen.push_back(e.type); });

    const std::vector<SystemEventType> kinds = {
        SystemEventType::SystemSleep,        SystemEventType::SystemWake,
        SystemEventType::DisplayChanged,     SystemEventType::SessionLocked,
        SystemEventType::SessionUnlocked,    SystemEventType::PowerStatusChanged,
    };
    for (auto k : kinds) {
        src.inject(make_event(k));
    }

    ASSERT_EQ(seen.size(), kinds.size());
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        EXPECT_EQ(seen[i], kinds[i]);
    }
}

// 分派順序穩定：依訂閱順序（SubscriptionId 遞增）分派。
TEST(SystemEvent, DispatchOrderFollowsSubscription) {
    NullSystemEventSource src;
    std::vector<int> order;
    src.subscribe([&](const SystemEvent&) { order.push_back(1); });
    src.subscribe([&](const SystemEvent&) { order.push_back(2); });
    src.subscribe([&](const SystemEvent&) { order.push_back(3); });

    src.inject(make_event(SystemEventType::PowerStatusChanged, "on battery"));

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

// listener 於回呼中解除自己：不影響本輪分派（快照語意），下一輪不再收。
TEST(SystemEvent, ListenerUnsubscribingDuringDispatchIsSafe) {
    NullSystemEventSource src;
    int self_hits = 0, other_hits = 0;
    SubscriptionId self_id = 0;
    self_id = src.subscribe([&](const SystemEvent&) {
        ++self_hits;
        src.unsubscribe(self_id);  // 回呼中解除自己
    });
    src.subscribe([&](const SystemEvent&) { ++other_hits; });

    src.inject(make_event(SystemEventType::SystemSleep));  // 本輪：兩者皆收
    src.inject(make_event(SystemEventType::SystemWake));   // 下一輪：self 已解除

    EXPECT_EQ(self_hits, 1);
    EXPECT_EQ(other_hits, 2);
}

}  // namespace
