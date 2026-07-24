// E5-03 生命週期事件 — 單元測試（gtest）
//
// 驗證：登記→合法轉換分派事件（from/to/surface 正確）、非法轉換被拒不改狀態不發事件、
// 終端 Destroyed 不可再轉出、未登記 surface 拒絕轉換、多訂閱者皆收且順序穩定、
// 解除訂閱後不再收、空 listener 拒收、回呼中改動訂閱安全、狀態機表窮舉、相位查詢。
// 全程平台中立、不依賴任何真實視窗系統。
#include "lifecycle_source.hpp"

#include <gtest/gtest.h>

#include <vector>

using ds::events::is_legal_transition;
using ds::events::LifecycleEvent;
using ds::events::LifecyclePhase;
using ds::events::LifecycleSource;
using ds::events::SubscriptionId;
using ds::events::SurfaceId;

namespace {

// 登記後初始相位為 Created。
TEST(LifecycleSource, CreateStartsAtCreatedPhase) {
    LifecycleSource src;
    EXPECT_TRUE(src.create(1));
    EXPECT_TRUE(src.has_surface(1));
    EXPECT_EQ(src.surface_count(), 1u);

    LifecyclePhase phase;
    ASSERT_TRUE(src.phase_of(1, phase));
    EXPECT_EQ(phase, LifecyclePhase::Created);
}

// 重複登記為 no-op，回 false，不重置相位。
TEST(LifecycleSource, DuplicateCreateIsNoOp) {
    LifecycleSource src;
    ASSERT_TRUE(src.create(1));
    ASSERT_TRUE(src.transition(1, LifecyclePhase::Shown));

    EXPECT_FALSE(src.create(1));  // 已存在
    LifecyclePhase phase;
    ASSERT_TRUE(src.phase_of(1, phase));
    EXPECT_EQ(phase, LifecyclePhase::Shown);  // 未被重置
    EXPECT_EQ(src.surface_count(), 1u);
}

// 合法轉換：改狀態並分派事件，事件內容正確。
TEST(LifecycleSource, LegalTransitionDispatchesEvent) {
    LifecycleSource src;
    src.create(7);
    std::vector<LifecycleEvent> events;
    src.subscribe([&](const LifecycleEvent& e) { events.push_back(e); });

    EXPECT_TRUE(src.transition(7, LifecyclePhase::Shown));
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].surface, 7u);
    EXPECT_EQ(events[0].from, LifecyclePhase::Created);
    EXPECT_EQ(events[0].to, LifecyclePhase::Shown);

    LifecyclePhase phase;
    ASSERT_TRUE(src.phase_of(7, phase));
    EXPECT_EQ(phase, LifecyclePhase::Shown);
}

// 一連串合法轉換：Created→Shown→Activated→Deactivated→Hidden→Shown→Destroyed。
TEST(LifecycleSource, FullLifecycleSequence) {
    LifecycleSource src;
    src.create(1);
    int fired = 0;
    src.subscribe([&](const LifecycleEvent&) { ++fired; });

    EXPECT_TRUE(src.transition(1, LifecyclePhase::Shown));
    EXPECT_TRUE(src.transition(1, LifecyclePhase::Activated));
    EXPECT_TRUE(src.transition(1, LifecyclePhase::Deactivated));
    EXPECT_TRUE(src.transition(1, LifecyclePhase::Hidden));
    EXPECT_TRUE(src.transition(1, LifecyclePhase::Shown));
    EXPECT_TRUE(src.transition(1, LifecyclePhase::Destroyed));
    EXPECT_EQ(fired, 6);

    LifecyclePhase phase;
    ASSERT_TRUE(src.phase_of(1, phase));
    EXPECT_EQ(phase, LifecyclePhase::Destroyed);
}

// 非法轉換被拒：不改狀態、不發事件。
TEST(LifecycleSource, IllegalTransitionRejected) {
    LifecycleSource src;
    src.create(1);  // Created
    int fired = 0;
    src.subscribe([&](const LifecycleEvent&) { ++fired; });

    // Created 不能直接 Activated。
    EXPECT_FALSE(src.transition(1, LifecyclePhase::Activated));
    EXPECT_EQ(fired, 0);
    LifecyclePhase phase;
    ASSERT_TRUE(src.phase_of(1, phase));
    EXPECT_EQ(phase, LifecyclePhase::Created);  // 未變
}

// 終端 Destroyed 不可再轉出（如不能從 destroyed 再 shown）。
TEST(LifecycleSource, DestroyedIsTerminal) {
    LifecycleSource src;
    src.create(1);
    ASSERT_TRUE(src.transition(1, LifecyclePhase::Shown));
    ASSERT_TRUE(src.transition(1, LifecyclePhase::Destroyed));

    int fired = 0;
    src.subscribe([&](const LifecycleEvent&) { ++fired; });
    EXPECT_FALSE(src.transition(1, LifecyclePhase::Shown));
    EXPECT_FALSE(src.transition(1, LifecyclePhase::Created));
    EXPECT_EQ(fired, 0);

    LifecyclePhase phase;
    ASSERT_TRUE(src.phase_of(1, phase));
    EXPECT_EQ(phase, LifecyclePhase::Destroyed);
}

// 自我轉換（from == to）視為非法。
TEST(LifecycleSource, SelfTransitionRejected) {
    LifecycleSource src;
    src.create(1);
    src.transition(1, LifecyclePhase::Shown);
    int fired = 0;
    src.subscribe([&](const LifecycleEvent&) { ++fired; });

    EXPECT_FALSE(src.transition(1, LifecyclePhase::Shown));
    EXPECT_EQ(fired, 0);
}

// 未登記的 surface：轉換被拒。
TEST(LifecycleSource, TransitionUnknownSurfaceRejected) {
    LifecycleSource src;
    int fired = 0;
    src.subscribe([&](const LifecycleEvent&) { ++fired; });

    EXPECT_FALSE(src.transition(99, LifecyclePhase::Shown));
    EXPECT_EQ(fired, 0);
    EXPECT_FALSE(src.has_surface(99));

    LifecyclePhase phase;
    EXPECT_FALSE(src.phase_of(99, phase));  // 未登記查詢回 false
}

// 多訂閱者皆收，且依訂閱順序分派。
TEST(LifecycleSource, MultipleListenersReceiveInOrder) {
    LifecycleSource src;
    src.create(1);
    std::vector<int> order;
    src.subscribe([&](const LifecycleEvent&) { order.push_back(1); });
    src.subscribe([&](const LifecycleEvent&) { order.push_back(2); });
    src.subscribe([&](const LifecycleEvent&) { order.push_back(3); });

    EXPECT_TRUE(src.transition(1, LifecyclePhase::Shown));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
    EXPECT_EQ(src.listener_count(), 3u);
}

// 解除訂閱後不再收；未知 id 解除為 no-op。
TEST(LifecycleSource, UnsubscribeStopsDelivery) {
    LifecycleSource src;
    src.create(1);
    int a = 0, b = 0;
    const SubscriptionId ida = src.subscribe([&](const LifecycleEvent&) { ++a; });
    src.subscribe([&](const LifecycleEvent&) { ++b; });

    EXPECT_TRUE(src.transition(1, LifecyclePhase::Shown));
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);

    EXPECT_TRUE(src.unsubscribe(ida));
    EXPECT_FALSE(src.unsubscribe(ida));   // 重複解除
    EXPECT_FALSE(src.unsubscribe(9999));  // 未知 id
    EXPECT_EQ(src.listener_count(), 1u);

    EXPECT_TRUE(src.transition(1, LifecyclePhase::Activated));
    EXPECT_EQ(a, 1);  // 已解除，未再收
    EXPECT_EQ(b, 2);
}

// 空 listener：拒收，回無效 id 0。
TEST(LifecycleSource, EmptyListenerRejected) {
    LifecycleSource src;
    const SubscriptionId id = src.subscribe(nullptr);
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(src.listener_count(), 0u);
}

// 回呼中解除自己的訂閱：本輪不崩、後續不再收。
TEST(LifecycleSource, UnsubscribeFromWithinCallbackIsSafe) {
    LifecycleSource src;
    src.create(1);
    int fired = 0;
    SubscriptionId id = 0;
    id = src.subscribe([&](const LifecycleEvent&) {
        ++fired;
        src.unsubscribe(id);
    });

    EXPECT_TRUE(src.transition(1, LifecyclePhase::Shown));
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(src.listener_count(), 0u);

    EXPECT_TRUE(src.transition(1, LifecyclePhase::Activated));
    EXPECT_EQ(fired, 1);  // 已解除，未再收
}

// 回呼中新增訂閱：不影響本輪分派（新訂閱下輪才生效）。
TEST(LifecycleSource, SubscribeFromWithinCallbackDoesNotAffectCurrentRound) {
    LifecycleSource src;
    src.create(1);
    int outer = 0, inner = 0;
    src.subscribe([&](const LifecycleEvent&) {
        ++outer;
        src.subscribe([&](const LifecycleEvent&) { ++inner; });
    });

    EXPECT_TRUE(src.transition(1, LifecyclePhase::Shown));
    EXPECT_EQ(outer, 1);
    EXPECT_EQ(inner, 0);  // 本輪快照不含新訂閱

    EXPECT_TRUE(src.transition(1, LifecyclePhase::Activated));
    EXPECT_EQ(outer, 2);
    EXPECT_EQ(inner, 1);  // 下輪才收（外層又新增一個，故 inner=1）
}

// 狀態機表窮舉：逐一驗證每條合法 / 非法邊與自我轉換。
TEST(LifecycleSource, LegalTransitionTableIsExhaustive) {
    using P = LifecyclePhase;
    const P all[] = {P::Created, P::Shown, P::Hidden,
                     P::Activated, P::Deactivated, P::Destroyed};

    auto legal = [](P from, P to) {
        switch (from) {
            case P::Created:     return to == P::Shown || to == P::Destroyed;
            case P::Shown:       return to == P::Activated || to == P::Deactivated ||
                                        to == P::Hidden || to == P::Destroyed;
            case P::Hidden:      return to == P::Shown || to == P::Destroyed;
            case P::Activated:   return to == P::Deactivated || to == P::Hidden ||
                                        to == P::Destroyed;
            case P::Deactivated: return to == P::Activated || to == P::Shown ||
                                        to == P::Hidden || to == P::Destroyed;
            case P::Destroyed:   return false;
        }
        return false;
    };

    for (P from : all) {
        for (P to : all) {
            const bool expected = (from != to) && legal(from, to);
            EXPECT_EQ(is_legal_transition(from, to), expected)
                << "from=" << static_cast<int>(from)
                << " to=" << static_cast<int>(to);
        }
    }
}

// Destroyed 為終端：對任何目標皆非法。
TEST(LifecycleSource, DestroyedHasNoLegalOutgoing) {
    using P = LifecyclePhase;
    for (P to : {P::Created, P::Shown, P::Hidden, P::Activated,
                 P::Deactivated, P::Destroyed}) {
        EXPECT_FALSE(is_legal_transition(P::Destroyed, to));
    }
}

}  // namespace
