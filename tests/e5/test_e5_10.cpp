// E5-10 逾時計時器 — 單元測試（gtest）
//
// 以注入的邏輯時間驗證：
//   一次性 timeout 觸發後自動移除、重複 interval 週期觸發、取消後不再觸發、
//   剩餘查詢正確、多計時器獨立、單次跨多週期多觸發、餘量保留不漂移、
//   一次性餘量丟棄、無效逾時拒絕、回呼中設定/取消安全。全程不依賴真實時鐘。
#include "timeout_timer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

using ds::events::TimeoutTimer;
using ds::events::TimerEvent;
using ds::events::TimerId;

namespace {

// 一次性逾時：達 delay 觸發一次，事件內容正確，隨即自動移除。
TEST(TimeoutTimer, OneShotFiresOnceThenRemoved) {
    TimeoutTimer t;
    std::vector<TimerEvent> events;
    const TimerId id = t.set_timeout(5, [&](const TimerEvent& e) { events.push_back(e); });

    EXPECT_EQ(t.advance(4), 0u);  // 未達
    EXPECT_TRUE(t.is_active(id));
    EXPECT_EQ(t.advance(1), 1u);  // 達 5 -> 觸發
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].id, id);
    EXPECT_EQ(events[0].now, 5u);
    EXPECT_EQ(events[0].count, 1u);
    EXPECT_FALSE(events[0].repeating);

    // 自動移除：再推進不再觸發，且不再是 active。
    EXPECT_FALSE(t.is_active(id));
    EXPECT_EQ(t.active_count(), 0u);
    EXPECT_EQ(t.advance(100), 0u);
    EXPECT_EQ(events.size(), 1u);
}

// 一次性逾時：單次 advance 跨越 delay，只觸發一次，餘量丟棄（不變重複）。
TEST(TimeoutTimer, OneShotDiscardsRemainderOnLargeAdvance) {
    TimeoutTimer t;
    int fired = 0;
    t.set_timeout(3, [&](const TimerEvent&) { ++fired; });

    EXPECT_EQ(t.advance(10), 1u);  // 跨過 3，仍只一次
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(t.active_count(), 0u);
}

// 重複逾時：每 interval 週期觸發，計次連續。
TEST(TimeoutTimer, IntervalFiresPeriodically) {
    TimeoutTimer t;
    std::vector<std::uint64_t> counts;
    const TimerId id = t.set_interval(3, [&](const TimerEvent& e) { counts.push_back(e.count); });

    EXPECT_EQ(t.advance(3), 1u);
    EXPECT_EQ(t.advance(3), 1u);
    EXPECT_EQ(t.advance(3), 1u);
    ASSERT_EQ(counts.size(), 3u);
    EXPECT_EQ(counts[0], 1u);
    EXPECT_EQ(counts[1], 2u);
    EXPECT_EQ(counts[2], 3u);
    EXPECT_TRUE(t.is_active(id));  // 重複計時器持續存在
}

// 重複逾時：單次 advance 跨越多個週期，同一計時器多次觸發，且事件標記 repeating。
TEST(TimeoutTimer, IntervalFiresMultipleTimesOnLargeAdvance) {
    TimeoutTimer t;
    std::vector<TimerEvent> events;
    t.set_interval(2, [&](const TimerEvent& e) { events.push_back(e); });

    EXPECT_EQ(t.advance(7), 3u);  // 7 / 2 = 3 次（餘 1）
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].count, 1u);
    EXPECT_EQ(events[1].count, 2u);
    EXPECT_EQ(events[2].count, 3u);
    EXPECT_TRUE(events[2].repeating);
}

// 重複逾時餘量保留、不漂移：interval=5，advance(7) 觸發 1 次後餘 2，再 advance(3) 才滿 5。
TEST(TimeoutTimer, IntervalKeepsRemainderNoDrift) {
    TimeoutTimer t;
    int fired = 0;
    const TimerId id = t.set_interval(5, [&](const TimerEvent&) { ++fired; });

    EXPECT_EQ(t.advance(7), 1u);  // 觸發 1 次，餘 2 -> 距下次還需 3
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(t.remaining(id), std::optional<std::uint64_t>(3u));
    EXPECT_EQ(t.advance(2), 0u);  // 餘 4，未滿；距下次還需 1
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(t.remaining(id), std::optional<std::uint64_t>(1u));
    EXPECT_EQ(t.advance(1), 1u);  // 滿 5，觸發
    EXPECT_EQ(fired, 2);
}

// 剩餘查詢：建立後、推進後、未知 id。
TEST(TimeoutTimer, RemainingReportsTicksUntilNextFire) {
    TimeoutTimer t;
    const TimerId id = t.set_timeout(10, [](const TimerEvent&) {});
    EXPECT_EQ(t.remaining(id), std::optional<std::uint64_t>(10u));

    t.advance(4);
    EXPECT_EQ(t.remaining(id), std::optional<std::uint64_t>(6u));

    EXPECT_EQ(t.remaining(9999), std::nullopt);  // 未知 id
    t.advance(6);                                // 觸發並移除
    EXPECT_EQ(t.remaining(id), std::nullopt);    // 已移除
}

// 取消後不再觸發，重複取消回 false。
TEST(TimeoutTimer, CancelStopsFiring) {
    TimeoutTimer t;
    int fired = 0;
    const TimerId id = t.set_interval(2, [&](const TimerEvent&) { ++fired; });

    EXPECT_EQ(t.advance(2), 1u);
    EXPECT_EQ(fired, 1);

    EXPECT_TRUE(t.cancel(id));
    EXPECT_EQ(t.active_count(), 0u);
    EXPECT_EQ(t.advance(10), 0u);  // 已取消
    EXPECT_EQ(fired, 1);

    EXPECT_FALSE(t.cancel(id));  // 重複取消回 false
}

// 多計時器獨立累計（一次性 + 重複並存）。
TEST(TimeoutTimer, MultipleTimersAreIndependent) {
    TimeoutTimer t;
    int a = 0, b = 0;
    t.set_timeout(4, [&](const TimerEvent&) { ++a; });   // 一次性：於 t=4 觸發後消失
    t.set_interval(3, [&](const TimerEvent&) { ++b; });  // 重複：t=3,6 觸發

    // 推進 6：a 觸發 1 次（4），b 觸發 2 次（3,6），共 3。
    EXPECT_EQ(t.advance(6), 3u);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 2);
    EXPECT_EQ(t.active_count(), 1u);  // 只剩重複計時器
}

// tick() 等同 advance(1)。
TEST(TimeoutTimer, TickAdvancesByOne) {
    TimeoutTimer t;
    int fired = 0;
    t.set_timeout(2, [&](const TimerEvent&) { ++fired; });

    EXPECT_EQ(t.tick(), 0u);  // now=1
    EXPECT_EQ(t.tick(), 1u);  // now=2 -> 觸發
    EXPECT_EQ(t.now(), 2u);
    EXPECT_EQ(fired, 1);
}

// 無效逾時（0）：拒絕，回無效 id，不影響現存。
TEST(TimeoutTimer, RejectsZeroDelay) {
    TimeoutTimer t;
    EXPECT_EQ(t.set_timeout(0, [](const TimerEvent&) {}), 0u);
    EXPECT_EQ(t.set_interval(0, [](const TimerEvent&) {}), 0u);
    EXPECT_EQ(t.active_count(), 0u);
    EXPECT_EQ(t.advance(100), 0u);
}

// advance(0)：不推進、不觸發。
TEST(TimeoutTimer, ZeroAdvanceIsNoOp) {
    TimeoutTimer t;
    int fired = 0;
    t.set_timeout(1, [&](const TimerEvent&) { ++fired; });
    EXPECT_EQ(t.advance(0), 0u);
    EXPECT_EQ(t.now(), 0u);
    EXPECT_EQ(fired, 0);
}

// 於回呼中取消另一計時器：本次派發不崩、被取消者後續不再觸發。
TEST(TimeoutTimer, CancelFromWithinCallbackIsSafe) {
    TimeoutTimer t;
    int a = 0, b = 0;
    TimerId other = 0;
    t.set_interval(1, [&](const TimerEvent&) {
        ++a;
        if (other != 0) {
            t.cancel(other);  // 取消 B
        }
    });
    other = t.set_interval(1, [&](const TimerEvent&) { ++b; });

    // 首次 advance：A、B 皆在本輪 pending 中（狀態先結算），A 的回呼取消 B。
    EXPECT_EQ(t.advance(1), 2u);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
    EXPECT_FALSE(t.is_active(other));

    // 之後只剩 A 觸發。
    EXPECT_EQ(t.advance(3), 3u);
    EXPECT_EQ(a, 4);
    EXPECT_EQ(b, 1);
}

// 於回呼中新增計時器：不破壞本次派發，新計時器於後續才計時。
TEST(TimeoutTimer, SetFromWithinCallbackIsSafe) {
    TimeoutTimer t;
    int spawned = 0;
    bool added = false;
    t.set_timeout(1, [&](const TimerEvent&) {
        if (!added) {
            added = true;
            t.set_timeout(2, [&](const TimerEvent&) { ++spawned; });
        }
    });

    EXPECT_EQ(t.advance(1), 1u);   // 觸發原一次性；於回呼中新增一個 delay=2
    EXPECT_EQ(spawned, 0);
    EXPECT_EQ(t.active_count(), 1u);
    EXPECT_EQ(t.advance(2), 1u);   // 新計時器到期
    EXPECT_EQ(spawned, 1);
}

}  // namespace
