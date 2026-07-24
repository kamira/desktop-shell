// E5-04 心跳事件 — 單元測試（gtest）
//
// 以注入的邏輯時間驗證：推進未達間隔不觸發、達間隔觸發、多次推進累計、
// 單次跨多間隔多觸發、取消訂閱後不再觸發、餘量保留不漂移、事件內容正確、
// 多訂閱獨立、無效間隔拒絕。全程不依賴真實時鐘。
#include "heartbeat_source.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using ds::events::HeartbeatEvent;
using ds::events::HeartbeatSource;
using ds::events::SubscriptionId;

namespace {

// 推進未達間隔：不觸發。
TEST(HeartbeatSource, DoesNotFireBeforeInterval) {
    HeartbeatSource hb;
    int fired = 0;
    hb.subscribe(5, [&](const HeartbeatEvent&) { ++fired; });

    EXPECT_EQ(hb.advance(4), 0u);
    EXPECT_EQ(fired, 0);
    EXPECT_EQ(hb.now(), 4u);
}

// 推進達間隔：觸發一次，事件內容正確。
TEST(HeartbeatSource, FiresOnceAtInterval) {
    HeartbeatSource hb;
    std::vector<HeartbeatEvent> events;
    const SubscriptionId id = hb.subscribe(5, [&](const HeartbeatEvent& e) { events.push_back(e); });

    EXPECT_EQ(hb.advance(5), 1u);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].id, id);
    EXPECT_EQ(events[0].interval, 5u);
    EXPECT_EQ(events[0].now, 5u);
    EXPECT_EQ(events[0].count, 1u);
}

// 多次小步推進累計跨過間隔即觸發（累計、不丟脈衝）。
TEST(HeartbeatSource, AccumulatesAcrossAdvances) {
    HeartbeatSource hb;
    int fired = 0;
    hb.subscribe(3, [&](const HeartbeatEvent&) { ++fired; });

    EXPECT_EQ(hb.advance(1), 0u);  // 累計 1
    EXPECT_EQ(hb.advance(1), 0u);  // 累計 2
    EXPECT_EQ(hb.advance(1), 1u);  // 累計 3 -> 觸發
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(hb.advance(3), 1u);  // 再一輪
    EXPECT_EQ(fired, 2);
}

// 單次推進跨越多個間隔：同一訂閱多次觸發，計次連續。
TEST(HeartbeatSource, FiresMultipleTimesOnLargeAdvance) {
    HeartbeatSource hb;
    std::vector<std::uint64_t> counts;
    hb.subscribe(2, [&](const HeartbeatEvent& e) { counts.push_back(e.count); });

    EXPECT_EQ(hb.advance(7), 3u);  // 7 / 2 = 3 次（餘 1）
    ASSERT_EQ(counts.size(), 3u);
    EXPECT_EQ(counts[0], 1u);
    EXPECT_EQ(counts[1], 2u);
    EXPECT_EQ(counts[2], 3u);
}

// 餘量保留、不漂移：interval=5，advance(7) 觸發 1 次後餘 2，再 advance(3) 才滿 5。
TEST(HeartbeatSource, KeepsRemainderNoDrift) {
    HeartbeatSource hb;
    int fired = 0;
    hb.subscribe(5, [&](const HeartbeatEvent&) { ++fired; });

    EXPECT_EQ(hb.advance(7), 1u);  // 餘 2
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(hb.advance(2), 0u);  // 餘 4，未滿
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(hb.advance(1), 1u);  // 滿 5，觸發
    EXPECT_EQ(fired, 2);
}

// 取消訂閱後不再觸發。
TEST(HeartbeatSource, DoesNotFireAfterUnsubscribe) {
    HeartbeatSource hb;
    int fired = 0;
    const SubscriptionId id = hb.subscribe(2, [&](const HeartbeatEvent&) { ++fired; });

    EXPECT_EQ(hb.advance(2), 1u);
    EXPECT_EQ(fired, 1);

    EXPECT_TRUE(hb.unsubscribe(id));
    EXPECT_EQ(hb.subscription_count(), 0u);

    EXPECT_EQ(hb.advance(10), 0u);  // 已無訂閱
    EXPECT_EQ(fired, 1);

    EXPECT_FALSE(hb.unsubscribe(id));  // 重複取消回 false
}

// 多訂閱各自獨立累計。
TEST(HeartbeatSource, MultipleSubscribersAreIndependent) {
    HeartbeatSource hb;
    int a = 0, b = 0;
    hb.subscribe(2, [&](const HeartbeatEvent&) { ++a; });
    hb.subscribe(3, [&](const HeartbeatEvent&) { ++b; });

    // 推進 6：a 觸發 3 次（2,4,6），b 觸發 2 次（3,6），共 5。
    EXPECT_EQ(hb.advance(6), 5u);
    EXPECT_EQ(a, 3);
    EXPECT_EQ(b, 2);
    EXPECT_EQ(hb.subscription_count(), 2u);
}

// tick() 等同 advance(1)。
TEST(HeartbeatSource, TickAdvancesByOne) {
    HeartbeatSource hb;
    int fired = 0;
    hb.subscribe(2, [&](const HeartbeatEvent&) { ++fired; });

    EXPECT_EQ(hb.tick(), 0u);  // now=1
    EXPECT_EQ(hb.tick(), 1u);  // now=2 -> 觸發
    EXPECT_EQ(hb.now(), 2u);
    EXPECT_EQ(fired, 1);
}

// 無效間隔（0）：拒絕訂閱，回無效 id，不影響現存訂閱。
TEST(HeartbeatSource, RejectsZeroInterval) {
    HeartbeatSource hb;
    const SubscriptionId id = hb.subscribe(0, [](const HeartbeatEvent&) {});
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(hb.subscription_count(), 0u);
    EXPECT_EQ(hb.advance(100), 0u);
}

// advance(0)：不推進、不觸發。
TEST(HeartbeatSource, ZeroAdvanceIsNoOp) {
    HeartbeatSource hb;
    int fired = 0;
    hb.subscribe(1, [&](const HeartbeatEvent&) { ++fired; });
    EXPECT_EQ(hb.advance(0), 0u);
    EXPECT_EQ(hb.now(), 0u);
    EXPECT_EQ(fired, 0);
}

// 於回呼中取消自身訂閱：本次派發不崩、後續不再觸發。
TEST(HeartbeatSource, UnsubscribeFromWithinCallbackIsSafe) {
    HeartbeatSource hb;
    int fired = 0;
    SubscriptionId id = 0;
    id = hb.subscribe(1, [&](const HeartbeatEvent&) {
        ++fired;
        hb.unsubscribe(id);
    });

    EXPECT_EQ(hb.advance(1), 1u);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(hb.subscription_count(), 0u);
    EXPECT_EQ(hb.advance(5), 0u);
    EXPECT_EQ(fired, 1);
}

}  // namespace
