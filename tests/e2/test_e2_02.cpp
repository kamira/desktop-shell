// E2-02 採集頻率分級與除頻 — 契約測試（gtest）
//
// 覆蓋：分級位階 / 比較 / 字串、policy 間隔映射（含 OnDemand 非週期、set_interval 夾值 /
// 鏈式）、排程器登記 / 追蹤 / 有效分級查詢、**各分級間隔**（High/Normal/Low 於固定 tick 數
// 內的採集次數恰符合間隔）、**除頻合併**（多消費者同一指標只採一份、按最高需求頻率供給）、
// 撤銷需求後的降頻 / 停採、**on-demand**（不週期、request_now 才採一次）、以及邊界
// （時間倒退、跨多間隔不補採、間隔 0 夾值、多指標決定性順序）。
// 相位 1：純邏輯、注入邏輯 tick，不含任何平台分支 / 真實時鐘。
#include "sampling.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using ds::metrics::DemandId;
using ds::metrics::higher_tier;
using ds::metrics::is_periodic;
using ds::metrics::MetricId;
using ds::metrics::SamplingPolicy;
using ds::metrics::SamplingScheduler;
using ds::metrics::SamplingTier;
using ds::metrics::Tick;
using ds::metrics::tier_rank;
using ds::metrics::to_string;

namespace {

// 計數輔助：從 tick 0 起逐格推進到 end_exclusive-1，回傳 id 出現的採集次數。
int count_samples(SamplingScheduler& s, const MetricId& id, Tick end_exclusive) {
    int n = 0;
    for (Tick t = 0; t < end_exclusive; ++t) {
        std::vector<MetricId> due = s.advance(t);
        n += static_cast<int>(std::count(due.begin(), due.end(), id));
    }
    return n;
}

// ===========================================================================
// SamplingTier（分級位階 / 比較 / 述詞 / 字串）
// ===========================================================================
TEST(SamplingTier, RankOrdersHighToLow) {
    EXPECT_GT(tier_rank(SamplingTier::High), tier_rank(SamplingTier::Normal));
    EXPECT_GT(tier_rank(SamplingTier::Normal), tier_rank(SamplingTier::Low));
    EXPECT_GT(tier_rank(SamplingTier::Low), tier_rank(SamplingTier::OnDemand));
}

TEST(SamplingTier, HigherTierPicksMoreFrequent) {
    EXPECT_EQ(higher_tier(SamplingTier::Normal, SamplingTier::High), SamplingTier::High);
    EXPECT_EQ(higher_tier(SamplingTier::High, SamplingTier::Normal), SamplingTier::High);
    EXPECT_EQ(higher_tier(SamplingTier::Low, SamplingTier::OnDemand), SamplingTier::Low);
    // 位階相同回第一引數。
    EXPECT_EQ(higher_tier(SamplingTier::Normal, SamplingTier::Normal), SamplingTier::Normal);
}

TEST(SamplingTier, IsPeriodicExcludesOnDemand) {
    EXPECT_TRUE(is_periodic(SamplingTier::High));
    EXPECT_TRUE(is_periodic(SamplingTier::Normal));
    EXPECT_TRUE(is_periodic(SamplingTier::Low));
    EXPECT_FALSE(is_periodic(SamplingTier::OnDemand));
}

TEST(SamplingTier, ToStringStable) {
    EXPECT_EQ(std::string(to_string(SamplingTier::High)), "high");
    EXPECT_EQ(std::string(to_string(SamplingTier::Normal)), "normal");
    EXPECT_EQ(std::string(to_string(SamplingTier::Low)), "low");
    EXPECT_EQ(std::string(to_string(SamplingTier::OnDemand)), "on-demand");
}

// ===========================================================================
// SamplingPolicy（分級 → 間隔映射）
// ===========================================================================
TEST(SamplingPolicy, DefaultsSpreadTiers) {
    SamplingPolicy p = SamplingPolicy::defaults();
    ASSERT_TRUE(p.interval(SamplingTier::High).has_value());
    ASSERT_TRUE(p.interval(SamplingTier::Normal).has_value());
    ASSERT_TRUE(p.interval(SamplingTier::Low).has_value());
    EXPECT_EQ(*p.interval(SamplingTier::High), 1u);
    EXPECT_EQ(*p.interval(SamplingTier::Normal), 8u);
    EXPECT_EQ(*p.interval(SamplingTier::Low), 64u);
    // 級距單調：越高頻間隔越短。
    EXPECT_LT(*p.interval(SamplingTier::High), *p.interval(SamplingTier::Normal));
    EXPECT_LT(*p.interval(SamplingTier::Normal), *p.interval(SamplingTier::Low));
}

TEST(SamplingPolicy, OnDemandHasNoInterval) {
    SamplingPolicy p;
    EXPECT_FALSE(p.interval(SamplingTier::OnDemand).has_value());
}

TEST(SamplingPolicy, SetIntervalOverridesAndChains) {
    SamplingPolicy p;
    p.set_interval(SamplingTier::High, 2).set_interval(SamplingTier::Normal, 20);
    EXPECT_EQ(*p.interval(SamplingTier::High), 2u);
    EXPECT_EQ(*p.interval(SamplingTier::Normal), 20u);
    EXPECT_EQ(*p.interval(SamplingTier::Low), 64u);  // 未動保持預設
}

TEST(SamplingPolicy, SetIntervalClampsZeroToOne) {
    SamplingPolicy p;
    p.set_interval(SamplingTier::Low, 0);
    EXPECT_EQ(*p.interval(SamplingTier::Low), 1u);  // 間隔 0 無意義 → 夾到 1
}

TEST(SamplingPolicy, SetIntervalOnOnDemandIsNoop) {
    SamplingPolicy p;
    p.set_interval(SamplingTier::OnDemand, 5);
    EXPECT_FALSE(p.interval(SamplingTier::OnDemand).has_value());  // 恆非週期
}

// ===========================================================================
// SamplingScheduler — 登記 / 追蹤 / 有效分級查詢
// ===========================================================================
TEST(Scheduler, StartsEmptyAtTickZero) {
    SamplingScheduler s;
    EXPECT_EQ(s.now(), 0u);
    EXPECT_EQ(s.metric_count(), 0u);
    EXPECT_FALSE(s.tracks("cpu.usage"));
    EXPECT_FALSE(s.effective_tier("cpu.usage").has_value());
    EXPECT_FALSE(s.effective_interval("cpu.usage").has_value());
    EXPECT_FALSE(s.next_due("cpu.usage").has_value());
    EXPECT_EQ(s.demand_count("cpu.usage"), 0u);
}

TEST(Scheduler, AddDemandTracksMetric) {
    SamplingScheduler s;
    DemandId d = s.add_demand("cpu.usage", SamplingTier::Normal);
    EXPECT_NE(d, 0u);
    EXPECT_TRUE(s.tracks("cpu.usage"));
    EXPECT_EQ(s.metric_count(), 1u);
    EXPECT_EQ(s.demand_count("cpu.usage"), 1u);
    ASSERT_TRUE(s.effective_tier("cpu.usage").has_value());
    EXPECT_EQ(*s.effective_tier("cpu.usage"), SamplingTier::Normal);
    ASSERT_TRUE(s.effective_interval("cpu.usage").has_value());
    EXPECT_EQ(*s.effective_interval("cpu.usage"), 8u);
}

TEST(Scheduler, DemandIdsAreDistinct) {
    SamplingScheduler s;
    DemandId a = s.add_demand("m", SamplingTier::High);
    DemandId b = s.add_demand("m", SamplingTier::Low);
    EXPECT_NE(a, b);
    EXPECT_EQ(s.demand_count("m"), 2u);
}

// ===========================================================================
// 各分級間隔：固定 tick 數內的採集次數恰符合分級
// ===========================================================================
TEST(Tiering, HighSamplesEveryTick) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::High);   // 間隔 1
    // t=0..63（64 次 advance）→ 每 tick 一採 → 64 次。
    EXPECT_EQ(count_samples(s, "m", 64), 64);
}

TEST(Tiering, NormalSamplesEveryEighthTick) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::Normal);  // 間隔 8
    // t=0,8,16,24,32,40,48,56 → 8 次（下一次在 t=64，超出範圍）。
    EXPECT_EQ(count_samples(s, "m", 64), 8);
}

TEST(Tiering, LowSamplesOncePerSixtyFourTicks) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::Low);     // 間隔 64
    // t=0 首採，下一次在 t=64 → 於 [0,64) 恰 1 次。
    EXPECT_EQ(count_samples(s, "m", 64), 1);
}

TEST(Tiering, CustomPolicyChangesCadence) {
    SamplingPolicy p;
    p.set_interval(SamplingTier::Normal, 4);  // 加密 Normal
    SamplingScheduler s(p);
    s.add_demand("m", SamplingTier::Normal);
    // t=0,4,8,...,60 → 16 次。
    EXPECT_EQ(count_samples(s, "m", 64), 16);
}

TEST(Tiering, FirstSampleHappensAtStartTick) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::Low);
    ASSERT_TRUE(s.next_due("m").has_value());
    EXPECT_EQ(*s.next_due("m"), 0u);          // 首採排在目前 tick
    std::vector<MetricId> due = s.advance(0);
    EXPECT_EQ(due.size(), 1u);
    EXPECT_EQ(*s.next_due("m"), 64u);         // 之後排到一整個間隔後
}

// ===========================================================================
// 除頻合併：多消費者要同一指標 → 合併採集、按最高需求頻率供給
// ===========================================================================
TEST(Coalescing, EffectiveTierIsHighestDemand) {
    SamplingScheduler s;
    s.add_demand("cpu.usage", SamplingTier::Low);
    s.add_demand("cpu.usage", SamplingTier::High);   // 最高頻
    s.add_demand("cpu.usage", SamplingTier::Normal);
    EXPECT_EQ(s.demand_count("cpu.usage"), 3u);
    ASSERT_TRUE(s.effective_tier("cpu.usage").has_value());
    EXPECT_EQ(*s.effective_tier("cpu.usage"), SamplingTier::High);
    EXPECT_EQ(*s.effective_interval("cpu.usage"), 1u);
}

TEST(Coalescing, OneSampleServesAllConsumers) {
    SamplingScheduler s;
    s.add_demand("cpu.usage", SamplingTier::Normal);
    s.add_demand("cpu.usage", SamplingTier::High);
    // 兩消費者其一為 High → 合併後每 tick 採一份，但每次 advance 該指標**只出現一次**
    // （不是每消費者各一份）。
    for (Tick t = 0; t < 10; ++t) {
        std::vector<MetricId> due = s.advance(t);
        EXPECT_EQ(std::count(due.begin(), due.end(), MetricId("cpu.usage")), 1);
    }
    // 供給頻率 = 最高需求（High/間隔1）：[0,10) 內 10 次。
    SamplingScheduler s2;
    s2.add_demand("cpu.usage", SamplingTier::Normal);
    s2.add_demand("cpu.usage", SamplingTier::High);
    EXPECT_EQ(count_samples(s2, "cpu.usage", 10), 10);
}

TEST(Coalescing, RaisingFrequencyPullsNextSampleEarlier) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::Low);     // 間隔 64
    s.advance(0);                             // 首採，next_due = 64
    EXPECT_EQ(*s.next_due("m"), 64u);
    s.add_demand("m", SamplingTier::High);    // 拉高頻率 → 提前
    // 新的下次時機不晚於 now(0)+1 = 1。
    EXPECT_LE(*s.next_due("m"), 1u);
    EXPECT_EQ(*s.effective_interval("m"), 1u);
}

TEST(Coalescing, OnDemandDoesNotSuppressPeriodicDemand) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::OnDemand);
    s.add_demand("m", SamplingTier::Normal);  // 週期需求仍成立
    ASSERT_TRUE(s.effective_tier("m").has_value());
    EXPECT_EQ(*s.effective_tier("m"), SamplingTier::Normal);
    ASSERT_TRUE(s.effective_interval("m").has_value());
    EXPECT_EQ(*s.effective_interval("m"), 8u);
    EXPECT_EQ(count_samples(s, "m", 64), 8);
}

// ===========================================================================
// 撤銷需求：降頻 / 停採
// ===========================================================================
TEST(RemoveDemand, RemovingHighestDropsFrequency) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::Normal);
    DemandId high = s.add_demand("m", SamplingTier::High);
    EXPECT_EQ(*s.effective_tier("m"), SamplingTier::High);

    EXPECT_TRUE(s.remove_demand(high));
    EXPECT_EQ(s.demand_count("m"), 1u);
    ASSERT_TRUE(s.effective_tier("m").has_value());
    EXPECT_EQ(*s.effective_tier("m"), SamplingTier::Normal);  // 降回 Normal
    EXPECT_EQ(*s.effective_interval("m"), 8u);
}

TEST(RemoveDemand, RemovingLastStopsTracking) {
    SamplingScheduler s;
    DemandId d = s.add_demand("m", SamplingTier::High);
    EXPECT_TRUE(s.remove_demand(d));
    EXPECT_FALSE(s.tracks("m"));
    EXPECT_EQ(s.metric_count(), 0u);
    EXPECT_FALSE(s.effective_tier("m").has_value());
    // 停採後推進不再回傳該指標。
    EXPECT_EQ(count_samples(s, "m", 10), 0);
}

TEST(RemoveDemand, UnknownDemandReturnsFalse) {
    SamplingScheduler s;
    EXPECT_FALSE(s.remove_demand(999));
    s.add_demand("m", SamplingTier::High);
    EXPECT_FALSE(s.remove_demand(999));
}

TEST(RemoveDemand, DroppedCadenceTakesEffectAfterNextSample) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::Normal);
    DemandId high = s.add_demand("m", SamplingTier::High);
    // High 期間每 tick 採。
    for (Tick t = 0; t < 5; ++t) s.advance(t);  // now=4, next_due=5
    s.remove_demand(high);                       // 降回 Normal（間隔 8）
    // t=5 到期（沿用舊 next_due）採一次後，改以 Normal 間隔續行 → 下次在 5+8=13。
    std::vector<MetricId> at5 = s.advance(5);
    EXPECT_EQ(std::count(at5.begin(), at5.end(), MetricId("m")), 1);
    // t=6..12 皆不採。
    int between = 0;
    for (Tick t = 6; t < 13; ++t) {
        std::vector<MetricId> due = s.advance(t);
        between += static_cast<int>(std::count(due.begin(), due.end(), MetricId("m")));
    }
    EXPECT_EQ(between, 0);
    std::vector<MetricId> at13 = s.advance(13);
    EXPECT_EQ(std::count(at13.begin(), at13.end(), MetricId("m")), 1);
}

// ===========================================================================
// on-demand：不週期、只在 request_now 後採一次
// ===========================================================================
TEST(OnDemand, NeverSamplesPeriodically) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::OnDemand);
    EXPECT_TRUE(s.tracks("m"));
    EXPECT_EQ(*s.effective_tier("m"), SamplingTier::OnDemand);
    EXPECT_FALSE(s.effective_interval("m").has_value());  // 非週期
    EXPECT_FALSE(s.next_due("m").has_value());
    EXPECT_EQ(count_samples(s, "m", 100), 0);              // 一整段時間都不自動採
}

TEST(OnDemand, RequestNowFiresExactlyOnce) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::OnDemand);
    EXPECT_TRUE(s.advance(0).empty());
    EXPECT_TRUE(s.advance(1).empty());

    s.request_now("m");
    std::vector<MetricId> at2 = s.advance(2);
    EXPECT_EQ(std::count(at2.begin(), at2.end(), MetricId("m")), 1);  // 採一次
    // 之後不再自動採（旗標已清、非週期）。
    int after = 0;
    for (Tick t = 3; t < 20; ++t) {
        std::vector<MetricId> due = s.advance(t);
        after += static_cast<int>(std::count(due.begin(), due.end(), MetricId("m")));
    }
    EXPECT_EQ(after, 0);
}

TEST(OnDemand, RequestNowOnUnknownIsNoop) {
    SamplingScheduler s;
    s.request_now("nope");  // 未追蹤 → 不崩、不採
    std::vector<MetricId> due = s.advance(0);
    EXPECT_TRUE(due.empty());
}

TEST(RequestNow, ForcesImmediateRefreshOnPeriodicMetric) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::Low);   // 間隔 64
    EXPECT_EQ(s.advance(0).size(), 1u);      // 首採，next_due=64
    EXPECT_TRUE(s.advance(1).empty());       // 未到期
    s.request_now("m");
    std::vector<MetricId> at2 = s.advance(2);  // 強制刷新
    EXPECT_EQ(std::count(at2.begin(), at2.end(), MetricId("m")), 1);
    // 強制採集後仍以間隔續行（next_due = 2 + 64 = 66）。
    EXPECT_EQ(*s.next_due("m"), 66u);
}

// ===========================================================================
// 邊界：時間倒退、跨多間隔不補採、多指標決定性順序
// ===========================================================================
TEST(Advance, BackwardTimeIsIgnored) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::High);
    s.advance(5);
    EXPECT_EQ(s.now(), 5u);
    std::vector<MetricId> due = s.advance(3);  // 倒退
    EXPECT_TRUE(due.empty());
    EXPECT_EQ(s.now(), 5u);                    // now 不倒退
}

TEST(Advance, NoCatchUpBurstAcrossManyIntervals) {
    SamplingScheduler s;
    s.add_demand("m", SamplingTier::High);   // 間隔 1
    std::vector<MetricId> at0 = s.advance(0);
    EXPECT_EQ(std::count(at0.begin(), at0.end(), MetricId("m")), 1);
    // 一次跳過 100 個間隔：只採一份（不補採 100 份）——直接關係 idle 門檻。
    std::vector<MetricId> jump = s.advance(100);
    EXPECT_EQ(std::count(jump.begin(), jump.end(), MetricId("m")), 1);
    EXPECT_EQ(*s.next_due("m"), 101u);
}

TEST(Advance, DueMetricsFollowRegistrationOrder) {
    SamplingScheduler s;
    s.add_demand("m1", SamplingTier::High);
    s.add_demand("m2", SamplingTier::High);
    s.add_demand("m3", SamplingTier::High);
    std::vector<MetricId> due = s.advance(0);
    ASSERT_EQ(due.size(), 3u);
    EXPECT_EQ(due[0], "m1");
    EXPECT_EQ(due[1], "m2");
    EXPECT_EQ(due[2], "m3");
}

TEST(Advance, MixedTiersDueTogetherWhenAligned) {
    SamplingScheduler s;
    s.add_demand("fast", SamplingTier::High);    // 間隔 1
    s.add_demand("slow", SamplingTier::Normal);  // 間隔 8
    // t=0：兩者皆首採。
    std::vector<MetricId> at0 = s.advance(0);
    EXPECT_EQ(at0.size(), 2u);
    // t=1..7：只 fast。
    for (Tick t = 1; t < 8; ++t) {
        std::vector<MetricId> due = s.advance(t);
        EXPECT_EQ(due.size(), 1u);
        EXPECT_EQ(due[0], "fast");
    }
    // t=8：兩者再次對齊。
    std::vector<MetricId> at8 = s.advance(8);
    EXPECT_EQ(at8.size(), 2u);
}

}  // namespace
