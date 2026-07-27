// E4-09 動畫驅動源 — 單元測試（gtest）
//
// 以注入的邏輯時間（心跳 advance）驗證：心跳脈衝推進動畫、暫停 / 繼續、移除、
// 多動畫獨立、驅動源整體暫停、單次 advance 跨多間隔多次派發、幀資料正確、
// 解構自動解除心跳訂閱、無效輸入拒絕。全程不依賴真實時鐘。
#include "animation_driver.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "heartbeat_source.hpp"

using ds::events::HeartbeatSource;
using ds::render::AnimationDriver;
using ds::render::AnimationFrame;
using ds::render::AnimationId;
using ds::render::kInvalidAnimationId;
using ds::render::Tick;

namespace {

// 心跳脈衝推進動畫：每達一個節拍，回呼被觸發一次且 dt = 節拍。
TEST(AnimationDriver, HeartbeatDrivesAnimation) {
    HeartbeatSource hb;
    AnimationDriver drv(hb, /*pulse_interval=*/5);

    int fired = 0;
    Tick last_dt = 0;
    drv.add([&](const AnimationFrame& f) {
        ++fired;
        last_dt = f.dt;
    });

    EXPECT_EQ(fired, 0);
    hb.advance(4);  // 未達節拍
    EXPECT_EQ(fired, 0);
    hb.advance(1);  // 累計 5 → 脈衝一次
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(last_dt, 5u);
    hb.advance(5);  // 再一節拍
    EXPECT_EQ(fired, 2);
}

// 幀資料：elapsed 累計、frame 自 1 遞增、id 為該動畫。
TEST(AnimationDriver, FrameDataAccumulates) {
    HeartbeatSource hb;
    AnimationDriver drv(hb, /*pulse_interval=*/2);

    std::vector<AnimationFrame> frames;
    const AnimationId id = drv.add([&](const AnimationFrame& f) { frames.push_back(f); });

    hb.advance(6);  // 3 個節拍
    ASSERT_EQ(frames.size(), 3u);
    for (const auto& f : frames) {
        EXPECT_EQ(f.id, id);
        EXPECT_EQ(f.dt, 2u);
    }
    EXPECT_EQ(frames[0].frame, 1u);
    EXPECT_EQ(frames[0].elapsed, 2u);
    EXPECT_EQ(frames[1].frame, 2u);
    EXPECT_EQ(frames[1].elapsed, 4u);
    EXPECT_EQ(frames[2].frame, 3u);
    EXPECT_EQ(frames[2].elapsed, 6u);
}

// 單次 advance 跨多個節拍：同一動畫於該次被多次推進（心跳多脈衝）。
TEST(AnimationDriver, SingleAdvanceCrossesMultiplePulses) {
    HeartbeatSource hb;
    AnimationDriver drv(hb, /*pulse_interval=*/3);

    int fired = 0;
    drv.add([&](const AnimationFrame&) { ++fired; });

    hb.advance(10);  // 10 / 3 = 3 個節拍（餘 1 保留）
    EXPECT_EQ(fired, 3);
    hb.advance(2);   // 餘 1 + 2 = 3 → 再 1 個節拍
    EXPECT_EQ(fired, 4);
}

// 暫停 / 繼續：暫停期間不推進、不累計；繼續後照常。
TEST(AnimationDriver, PauseAndResume) {
    HeartbeatSource hb;
    AnimationDriver drv(hb, /*pulse_interval=*/1);

    int fired = 0;
    Tick last_elapsed = 0;
    const AnimationId id = drv.add([&](const AnimationFrame& f) {
        ++fired;
        last_elapsed = f.elapsed;
    });

    hb.advance(2);
    EXPECT_EQ(fired, 2);
    EXPECT_EQ(last_elapsed, 2u);

    EXPECT_TRUE(drv.pause(id));
    EXPECT_TRUE(drv.is_paused(id));
    EXPECT_FALSE(drv.pause(id));  // 已暫停：無狀態改變

    hb.advance(5);                // 暫停期間不推進
    EXPECT_EQ(fired, 2);
    EXPECT_EQ(last_elapsed, 2u);  // elapsed 未累計

    EXPECT_TRUE(drv.resume(id));
    EXPECT_FALSE(drv.is_paused(id));
    EXPECT_FALSE(drv.resume(id));  // 已運行：無狀態改變

    hb.advance(3);
    EXPECT_EQ(fired, 5);
    EXPECT_EQ(last_elapsed, 5u);  // 暫停未貢獻，接續 2 → 5
}

// 移除：移除後不再被推進。
TEST(AnimationDriver, RemoveStopsDelivery) {
    HeartbeatSource hb;
    AnimationDriver drv(hb, /*pulse_interval=*/1);

    int fired = 0;
    const AnimationId id = drv.add([&](const AnimationFrame&) { ++fired; });

    hb.advance(2);
    EXPECT_EQ(fired, 2);
    EXPECT_EQ(drv.animation_count(), 1u);

    EXPECT_TRUE(drv.remove(id));
    EXPECT_EQ(drv.animation_count(), 0u);
    EXPECT_FALSE(drv.remove(id));  // 已移除

    hb.advance(5);
    EXPECT_EQ(fired, 2);  // 不再觸發
}

// 多動畫獨立：各自的節拍推進、暫停互不影響。
TEST(AnimationDriver, MultipleAnimationsIndependent) {
    HeartbeatSource hb;
    AnimationDriver drv(hb, /*pulse_interval=*/1);

    int a = 0, b = 0;
    const AnimationId ia = drv.add([&](const AnimationFrame&) { ++a; });
    drv.add([&](const AnimationFrame&) { ++b; });
    EXPECT_EQ(drv.animation_count(), 2u);

    hb.advance(3);
    EXPECT_EQ(a, 3);
    EXPECT_EQ(b, 3);

    drv.pause(ia);   // 只暫停 a
    hb.advance(2);
    EXPECT_EQ(a, 3);  // a 不動
    EXPECT_EQ(b, 5);  // b 續推
}

// 驅動源整體暫停：任何動畫都不推進；繼續後恢復。
TEST(AnimationDriver, GlobalPause) {
    HeartbeatSource hb;
    AnimationDriver drv(hb, /*pulse_interval=*/1);

    int fired = 0;
    drv.add([&](const AnimationFrame&) { ++fired; });

    drv.pause_all();
    EXPECT_TRUE(drv.paused());
    hb.advance(4);
    EXPECT_EQ(fired, 0);

    drv.resume_all();
    EXPECT_FALSE(drv.paused());
    hb.advance(4);
    EXPECT_EQ(fired, 4);
}

// pulse_interval = 0 被夾到 1（心跳不接受間隔 0）。
TEST(AnimationDriver, PulseIntervalClampedToOne) {
    HeartbeatSource hb;
    AnimationDriver drv(hb, /*pulse_interval=*/0);
    EXPECT_EQ(drv.pulse_interval(), 1u);

    int fired = 0;
    drv.add([&](const AnimationFrame&) { ++fired; });
    hb.advance(3);
    EXPECT_EQ(fired, 3);
}

// 空回呼不註冊，回傳無效 id。
TEST(AnimationDriver, EmptyCallbackRejected) {
    HeartbeatSource hb;
    AnimationDriver drv(hb);
    const AnimationId id = drv.add(ds::render::AnimationCallback{});
    EXPECT_EQ(id, kInvalidAnimationId);
    EXPECT_EQ(drv.animation_count(), 0u);
}

// 未知 id 的暫停 / 繼續 / 移除 / 查詢皆安全回 false。
TEST(AnimationDriver, UnknownIdIsSafe) {
    HeartbeatSource hb;
    AnimationDriver drv(hb);
    EXPECT_FALSE(drv.pause(999));
    EXPECT_FALSE(drv.resume(999));
    EXPECT_FALSE(drv.remove(999));
    EXPECT_FALSE(drv.is_paused(999));
}

// 解構後自動解除心跳訂閱：心跳不再持有懸空回呼。
TEST(AnimationDriver, DestructorUnsubscribesFromHeartbeat) {
    HeartbeatSource hb;
    {
        AnimationDriver drv(hb, /*pulse_interval=*/1);
        drv.add([](const AnimationFrame&) {});
        EXPECT_EQ(hb.subscription_count(), 1u);
        hb.advance(1);
    }
    // 驅動源已解構 → 心跳訂閱數歸零，之後推進不觸碰已釋放物件。
    EXPECT_EQ(hb.subscription_count(), 0u);
    hb.advance(10);  // 不得崩潰
    SUCCEED();
}

// 於回呼中移除自身 / 新增動畫：本次派發不被破壞。
TEST(AnimationDriver, MutateDuringDispatchIsSafe) {
    HeartbeatSource hb;
    AnimationDriver drv(hb, /*pulse_interval=*/1);

    int self_fired = 0;
    int added_fired = 0;
    AnimationId self_id = kInvalidAnimationId;
    self_id = drv.add([&](const AnimationFrame&) {
        ++self_fired;
        drv.remove(self_id);                                    // 回呼中移除自身
        drv.add([&](const AnimationFrame&) { ++added_fired; });  // 回呼中新增
    });

    hb.advance(1);            // 第一脈衝：self 觸發一次，移除自身、新增另一動畫
    EXPECT_EQ(self_fired, 1);
    EXPECT_EQ(added_fired, 0);  // 新增者本次不追溯觸發

    hb.advance(1);            // 第二脈衝：self 已移除，只有新增者觸發
    EXPECT_EQ(self_fired, 1);
    EXPECT_EQ(added_fired, 1);
}

}  // namespace
