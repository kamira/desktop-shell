// E2-03 CPU 負載（總體與每核心）— 測試（gtest）
//
// 覆蓋：提供者身分、註冊到 E2-01 registry、總體 + 每核心實例列舉、
// 兩次取樣差分計算正確（注入兩份 tick → 算出 %）、0% / 100% 邊界、核心數變動、
// 經 E2-02 頻率採樣（除頻排程）、null 來源行為、直接比率來源、聚合總體差分、
// 計數器重置 / 無經過時間邊界、消費者只走 E2-01 抽象介面、範圍 bounded[0,100]、
// 重複註冊保守拒絕。相位 1：只驗介面 + 注入式來源行為，不含任何平台分支。
#include "cpu_load.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "metric.hpp"
#include "sampling.hpp"

using ds::metrics::Metric;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRegistry;
using ds::metrics::MetricValue;
using ds::metrics::SamplingScheduler;
using ds::metrics::SamplingTier;
using ds::sysinfo::CpuCoreTicks;
using ds::sysinfo::CpuLoadProvider;
using ds::sysinfo::CpuStatSource;
using ds::sysinfo::CpuTicksSample;
using ds::sysinfo::CpuUsageSample;
using ds::sysinfo::DifferencingCpuStatSource;
using ds::sysinfo::NullCpuStatSource;
using ds::sysinfo::NullCpuTickSource;
using ds::sysinfo::core_usage_ratio;
using ds::sysinfo::usage_from_delta;

namespace {

// 建一份 N 核的累積 tick 快照：各核 {busy, total} 相同。
CpuTicksSample makeTicks(std::vector<std::pair<std::uint64_t, std::uint64_t>> cores) {
    CpuTicksSample s;
    for (auto& c : cores) s.cores.push_back(CpuCoreTicks{c.first, c.second});
    return s;
}

// 直接比率來源：每核心固定比率。
std::shared_ptr<NullCpuStatSource> makeRatioSource(std::vector<double> ratios) {
    auto src = std::make_shared<NullCpuStatSource>();
    src->set_per_core(std::move(ratios));
    return src;
}

constexpr double kEps = 1e-9;

}  // namespace

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(CpuLoadProvider, ProviderIdIsStable) {
    CpuLoadProvider p{std::make_shared<NullCpuStatSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.cpu");
    EXPECT_EQ(std::string(CpuLoadProvider::kMetricId), "cpu.usage");
    EXPECT_EQ(std::string(CpuLoadProvider::kMetricName), "CPU Usage");
    EXPECT_EQ(std::string(CpuLoadProvider::kUnit), "%");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(CpuLoadProvider, IsMetricProvider) {
    CpuLoadProvider p{std::make_shared<NullCpuStatSource>()};
    MetricProvider& mp = p;  // 可上轉
    EXPECT_EQ(mp.provider_id(), "sysinfo.cpu");
}

// 預設採集分級 = High（CPU 負載宜跟手），可由建構子覆寫。
TEST(CpuLoadProvider, SamplingTierDefaultsHighOverridable) {
    CpuLoadProvider def{std::make_shared<NullCpuStatSource>()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::High);

    CpuLoadProvider low{std::make_shared<NullCpuStatSource>(),
                        CpuLoadProvider::kDefaultHistory, SamplingTier::Low};
    EXPECT_EQ(low.sampling_tier(), SamplingTier::Low);
}

// ===========================================================================
// 註冊 / 列舉：總體 + 每核心
// ===========================================================================
TEST(CpuLoadProvider, RegistersSingleMetricWithTotalAndPerCore) {
    // 4 核直接比率來源。
    auto src = makeRatioSource({0.10, 0.20, 0.30, 0.40});
    CpuLoadProvider p{src};
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);

    EXPECT_EQ(added, 1u);
    EXPECT_TRUE(reg.contains("cpu.usage"));
    auto metric = reg.get("cpu.usage");
    ASSERT_NE(metric, nullptr);

    // 一個總體 + 4 核心 = 5 實例（列舉順序：total 在前）。
    EXPECT_EQ(metric->instance_count(), 5u);
    EXPECT_EQ(p.core_count(), 4u);
    EXPECT_EQ(metric->instance(0).instance_id(), "total");
    EXPECT_EQ(metric->instance(0).label(), "CPU Total");
    EXPECT_EQ(metric->instance(1).instance_id(), "cpu0");
    EXPECT_EQ(metric->instance(1).label(), "Core 0");
    EXPECT_EQ(metric->instance(4).instance_id(), "cpu3");
    EXPECT_EQ(metric->instance(4).label(), "Core 3");
}

// 總體使用率 + 每核心使用率讀值正確（直接比率路徑，比率*100 = %）。
TEST(CpuLoadProvider, OverallAndPerCoreValues) {
    auto src = makeRatioSource({0.10, 0.20, 0.30, 0.40});  // 平均 0.25
    CpuLoadProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("cpu.usage");
    ASSERT_NE(m, nullptr);

    const auto* total = m->find_instance("total");
    ASSERT_NE(total, nullptr);
    ASSERT_TRUE(total->value().valid);
    EXPECT_NEAR(total->value().number, 25.0, kEps);  // 平均 25%

    const auto* c0 = m->find_instance("cpu0");
    const auto* c3 = m->find_instance("cpu3");
    ASSERT_NE(c0, nullptr);
    ASSERT_NE(c3, nullptr);
    EXPECT_NEAR(c0->value().number, 10.0, kEps);
    EXPECT_NEAR(c3->value().number, 40.0, kEps);
}

// ===========================================================================
// 兩次取樣差分：注入兩份 tick → 算出 %
// ===========================================================================
TEST(CpuLoadProvider, DifferencingFromTwoTickSamples) {
    // 單核：t0 {busy=0,total=0}，t1 {busy=50,total=100} → busyΔ/totalΔ = 50%。
    auto ticks = std::make_shared<NullCpuTickSource>(std::vector<CpuTicksSample>{
        makeTicks({{0, 0}}),
        makeTicks({{50, 100}}),
    });
    auto diff = std::make_shared<DifferencingCpuStatSource>(ticks);
    CpuLoadProvider p{diff};
    MetricRegistry reg;
    reg.add_provider(p);  // register 內首次 sample → 只有一份 tick → 無讀值
    auto m = reg.get("cpu.usage");
    ASSERT_NE(m, nullptr);

    // 首次（register 時）：差分僅一份 → 總體未知、無核心實例。
    EXPECT_FALSE(m->find_instance("total")->value().valid);
    EXPECT_EQ(p.core_count(), 0u);

    // 第二次採樣：讀第二份 tick，與第一份差分 → 50%。
    p.sample();
    const auto* total = m->find_instance("total");
    ASSERT_TRUE(total->value().valid);
    EXPECT_NEAR(total->value().number, 50.0, kEps);
    ASSERT_EQ(p.core_count(), 1u);
    EXPECT_NEAR(m->find_instance("cpu0")->value().number, 50.0, kEps);
}

// 差分自由函式：多核不同負載。
TEST(UsageFromDelta, PerCoreAndAggregateOverall) {
    // 兩核。core0：busyΔ=25/totalΔ=100 = 25%。core1：busyΔ=75/totalΔ=100 = 75%。
    CpuTicksSample prev = makeTicks({{0, 0}, {0, 0}});
    CpuTicksSample curr = makeTicks({{25, 100}, {75, 100}});
    CpuUsageSample u = usage_from_delta(prev, curr);

    ASSERT_TRUE(u.valid);
    ASSERT_EQ(u.per_core.size(), 2u);
    EXPECT_NEAR(u.per_core[0], 0.25, kEps);
    EXPECT_NEAR(u.per_core[1], 0.75, kEps);
    // 聚合總體：sum busyΔ(100) / sum totalΔ(200) = 50%。
    EXPECT_NEAR(u.overall, 0.50, kEps);
}

// 聚合總體差分 != 各核比率平均（當各核 totalΔ 不同時）。
TEST(UsageFromDelta, AggregateOverallWeightsByTotalDelta) {
    // core0：busyΔ=10/totalΔ=10 = 100%（totalΔ 小）。core1：busyΔ=0/totalΔ=90 = 0%。
    CpuTicksSample prev = makeTicks({{0, 0}, {0, 0}});
    CpuTicksSample curr = makeTicks({{10, 10}, {0, 90}});
    CpuUsageSample u = usage_from_delta(prev, curr);

    ASSERT_TRUE(u.valid);
    EXPECT_NEAR(u.per_core[0], 1.0, kEps);
    EXPECT_NEAR(u.per_core[1], 0.0, kEps);
    // 算術平均會是 50%；聚合差分 = 10/100 = 10%（正確地以 totalΔ 加權）。
    EXPECT_NEAR(u.overall, 0.10, kEps);
}

// ===========================================================================
// 0% / 100% 邊界
// ===========================================================================
TEST(CoreUsageRatio, ZeroAndFullLoadBoundaries) {
    // 0%：busyΔ=0。
    EXPECT_NEAR(core_usage_ratio({100, 500}, {100, 600}), 0.0, kEps);
    // 100%：busyΔ==totalΔ。
    EXPECT_NEAR(core_usage_ratio({100, 500}, {200, 600}), 1.0, kEps);
}

TEST(CpuLoadProvider, ZeroAndHundredPercentThroughProvider) {
    auto src = makeRatioSource({0.0, 1.0});  // 0% 與 100%
    CpuLoadProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("cpu.usage");
    EXPECT_NEAR(m->find_instance("cpu0")->value().number, 0.0, kEps);
    EXPECT_NEAR(m->find_instance("cpu1")->value().number, 100.0, kEps);
}

// 差分邊界：計數器重置 / 無經過時間 / busy 超過 total 皆保守。
TEST(CoreUsageRatio, ResetAndZeroElapsedAndOverflowGuards) {
    // 計數器重置（curr < prev）→ 0。
    EXPECT_NEAR(core_usage_ratio({500, 1000}, {10, 20}), 0.0, kEps);
    // 無經過時間（totalΔ==0）→ 0。
    EXPECT_NEAR(core_usage_ratio({100, 500}, {100, 500}), 0.0, kEps);
    // busyΔ > totalΔ（理論不該發生）→ 夾到 100%。
    EXPECT_NEAR(core_usage_ratio({0, 0}, {200, 100}), 1.0, kEps);
}

// ===========================================================================
// 核心數變動
// ===========================================================================
TEST(CpuLoadProvider, CoreCountGrowsAcrossSamples) {
    // t0：2 核零基準。t1：2 核。t2：4 核（上線兩顆）。
    auto ticks = std::make_shared<NullCpuTickSource>(std::vector<CpuTicksSample>{
        makeTicks({{0, 0}, {0, 0}}),
        makeTicks({{50, 100}, {50, 100}}),
        makeTicks({{100, 200}, {100, 200}, {30, 200}, {30, 200}}),
    });
    auto diff = std::make_shared<DifferencingCpuStatSource>(ticks);
    CpuLoadProvider p{diff};
    MetricRegistry reg;
    reg.add_provider(p);  // 首份 → 未知、0 核

    p.sample();  // 差分 t0→t1：2 核，各 50%
    EXPECT_EQ(p.core_count(), 2u);

    p.sample();  // 差分 t1→t2：共同核心數 min(2,4)=2 核（差分只對齊共同核心）
    // 註：差分以較小核心數對齊，故仍 2 核（誠實：t1 只有 2 核可比）。
    EXPECT_EQ(p.core_count(), 2u);
    auto m = reg.get("cpu.usage");
    // t1→t2 core0：busyΔ=50/totalΔ=100 = 50%。
    EXPECT_NEAR(m->find_instance("cpu0")->value().number, 50.0, kEps);
}

// 直接比率來源核心數增加 → 動態新增核心實例（既有參照不失效）。
TEST(CpuLoadProvider, DirectSourceCoreCountIncreaseAddsInstances) {
    auto src = makeRatioSource({0.5, 0.5});  // 2 核
    CpuLoadProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.core_count(), 2u);
    auto m = reg.get("cpu.usage");
    const auto* c0_before = m->find_instance("cpu0");

    src->set_per_core({0.5, 0.5, 0.5, 0.5});  // 增為 4 核
    p.sample();
    EXPECT_EQ(p.core_count(), 4u);
    EXPECT_EQ(m->instance_count(), 5u);  // total + 4
    // 既有 core0 參照仍有效（unique_ptr 持有實例）。
    EXPECT_EQ(m->find_instance("cpu0"), c0_before);
    EXPECT_NE(m->find_instance("cpu3"), nullptr);
}

// 核心數減少 → 多出的核心實例設為未知（不縮減、誠實表達下線）。
TEST(CpuLoadProvider, CoreCountDecreaseMarksMissingUnknown) {
    auto src = makeRatioSource({0.5, 0.5, 0.5, 0.5});  // 4 核
    CpuLoadProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.core_count(), 4u);

    src->set_per_core({0.5, 0.5});  // 降為 2 核
    p.sample();
    auto m = reg.get("cpu.usage");
    // 實例數不縮減（4 核實例仍在）。
    EXPECT_EQ(p.core_count(), 4u);
    EXPECT_TRUE(m->find_instance("cpu0")->value().valid);
    EXPECT_TRUE(m->find_instance("cpu1")->value().valid);
    EXPECT_FALSE(m->find_instance("cpu2")->value().valid);  // 下線 → 未知
    EXPECT_FALSE(m->find_instance("cpu3")->value().valid);
}

// ===========================================================================
// 經 E2-02 頻率採樣（除頻排程）
// ===========================================================================
TEST(CpuLoadProvider, SampledViaE2_02Scheduler) {
    // 一列累積 tick，每份 total 各 +100、busy 各 +50（→ 每次差分 50%）。
    std::vector<CpuTicksSample> seq;
    for (std::uint64_t k = 0; k <= 40; ++k) {
        seq.push_back(makeTicks({{50 * k, 100 * k}}));
    }
    auto ticks = std::make_shared<NullCpuTickSource>(std::move(seq));
    auto diff = std::make_shared<DifferencingCpuStatSource>(ticks);
    CpuLoadProvider p{diff};
    MetricRegistry reg;
    reg.add_provider(p);  // register 消耗第 0 份（基準）

    SamplingScheduler sched;  // 預設 High 間隔=1
    sched.add_demand(CpuLoadProvider::kMetricId, p.sampling_tier());

    // 推進 4 個 tick，每 tick High 皆到期 → 每次採樣呼叫 provider.sample()。
    int sampled = 0;
    for (ds::metrics::Tick t = 1; t <= 4; ++t) {
        auto due = sched.advance(t);
        for (const auto& id : due) {
            if (id == CpuLoadProvider::kMetricId) {
                p.sample();
                ++sampled;
            }
        }
    }
    EXPECT_EQ(sampled, 4);

    auto m = reg.get("cpu.usage");
    const auto& hist = m->find_instance("cpu0")->history();
    // 4 次有效採樣 → 歷史累積 4 筆，各 50%。
    EXPECT_EQ(hist.size(), 4u);
    EXPECT_NEAR(hist.latest(), 50.0, kEps);
}

// 除頻：多消費者同一指標合併，最高頻者供給。
TEST(CpuLoadProvider, DeFrequencyCoalescesDemands) {
    CpuLoadProvider p{makeRatioSource({0.5})};
    SamplingScheduler sched;
    auto d_low = sched.add_demand(CpuLoadProvider::kMetricId, SamplingTier::Low);
    sched.add_demand(CpuLoadProvider::kMetricId, SamplingTier::High);
    // 有效分級 = 最高頻者 High。
    ASSERT_TRUE(sched.effective_tier(CpuLoadProvider::kMetricId).has_value());
    EXPECT_EQ(*sched.effective_tier(CpuLoadProvider::kMetricId), SamplingTier::High);
    EXPECT_EQ(sched.demand_count(CpuLoadProvider::kMetricId), 2u);

    // 撤銷 Low 後仍被 High 追蹤。
    EXPECT_TRUE(sched.remove_demand(d_low));
    EXPECT_TRUE(sched.tracks(CpuLoadProvider::kMetricId));
    EXPECT_EQ(*sched.effective_tier(CpuLoadProvider::kMetricId), SamplingTier::High);
}

// ===========================================================================
// null 來源行為
// ===========================================================================
TEST(CpuLoadProvider, NullSourceIsConservative) {
    // source 為 null 指標：仍掛上指標，總體未知、無核心、不崩。
    CpuLoadProvider p{nullptr};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 1u);
    auto m = reg.get("cpu.usage");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->instance_count(), 1u);  // 僅總體
    EXPECT_FALSE(m->find_instance("total")->value().valid);
    EXPECT_EQ(p.core_count(), 0u);
    // sample() 不崩。
    p.sample();
    EXPECT_FALSE(m->find_instance("total")->value().valid);
}

TEST(CpuLoadProvider, NullStatSourceDefaultUnknown) {
    // NullCpuStatSource 預設（未注入）→ 無讀值。
    auto src = std::make_shared<NullCpuStatSource>();
    CpuLoadProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("cpu.usage");
    EXPECT_FALSE(m->find_instance("total")->value().valid);
    EXPECT_EQ(p.core_count(), 0u);

    // 之後注入資料 → sample() 後可讀。
    src->set_per_core({0.6});
    p.sample();
    EXPECT_TRUE(m->find_instance("total")->value().valid);
    EXPECT_NEAR(m->find_instance("cpu0")->value().number, 60.0, kEps);
}

// sample() 未 register_metrics 時為 no-op（不崩）。
TEST(CpuLoadProvider, SampleBeforeRegisterIsNoop) {
    CpuLoadProvider p{makeRatioSource({0.5})};
    p.sample();  // 尚未 register → no-op，不崩
    EXPECT_EQ(p.core_count(), 0u);
}

// DifferencingCpuStatSource 首次 sample 回未知（差分至少需兩份），無 tick 來源亦未知。
TEST(DifferencingCpuStatSource, FirstSampleUnknownThenValid) {
    auto ticks = std::make_shared<NullCpuTickSource>(std::vector<CpuTicksSample>{
        makeTicks({{0, 0}}),
        makeTicks({{25, 100}}),
    });
    DifferencingCpuStatSource diff{ticks};
    EXPECT_FALSE(diff.primed());
    EXPECT_FALSE(diff.sample().valid);  // 首份 → 未知
    EXPECT_TRUE(diff.primed());
    CpuUsageSample u = diff.sample();  // 差分 → 25%
    ASSERT_TRUE(u.valid);
    EXPECT_NEAR(u.overall, 0.25, kEps);

    // 無 tick 來源 → 未知。
    DifferencingCpuStatSource none{nullptr};
    EXPECT_FALSE(none.sample().valid);
}

// NullCpuTickSource：列盡持續回最後一份；空列回空快照。
TEST(NullCpuTickSource, ExhaustionAndEmpty) {
    NullCpuTickSource src{std::vector<CpuTicksSample>{makeTicks({{1, 2}})}};
    EXPECT_EQ(src.read().core_count(), 1u);
    // 列盡 → 持續回最後一份。
    EXPECT_EQ(src.read().cores[0], (CpuCoreTicks{1, 2}));

    NullCpuTickSource empty;
    EXPECT_TRUE(empty.read().empty());
}

// ===========================================================================
// 範圍 / 消費者範式 / 重複註冊
// ===========================================================================
TEST(CpuLoadProvider, RangeIsBoundedZeroToHundred) {
    CpuLoadProvider p{makeRatioSource({0.5})};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("cpu.usage");
    auto r = m->range();
    ASSERT_TRUE(r.is_bounded());
    EXPECT_NEAR(*r.min, 0.0, kEps);
    EXPECT_NEAR(*r.max, 100.0, kEps);
    // 50% 可正規化到 0.5。
    auto norm = r.normalized(50.0);
    ASSERT_TRUE(norm.has_value());
    EXPECT_NEAR(*norm, 0.5, kEps);
}

// 掛件風格消費者：只透過 E2-01 registry / Metric 抽象介面走訪，完全不觸及具體型別。
TEST(CpuLoadProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = makeRatioSource({0.20, 0.80});
    CpuLoadProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    // 消費者只認識 MetricRegistry + Metric + MetricInstance。
    std::shared_ptr<Metric> m = reg.get("cpu.usage");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), "%");
    double sum = 0.0;
    for (std::size_t i = 0; i < m->instance_count(); ++i) {
        const auto& inst = m->instance(i);
        if (inst.instance_id() != "total" && inst.value().valid) {
            sum += inst.value().number;
        }
    }
    EXPECT_NEAR(sum, 100.0, kEps);  // 20 + 80
}

TEST(CpuLoadProvider, DuplicateRegistrationRejected) {
    CpuLoadProvider p1{makeRatioSource({0.5})};
    CpuLoadProvider p2{makeRatioSource({0.9})};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 1u);
    // 第二個提供者掛同一 id → 保守拒絕（不覆寫既有）。
    EXPECT_EQ(reg.add_provider(p2), 0u);
    EXPECT_EQ(reg.size(), 1u);
}

// ===========================================================================
// 值模型單元
// ===========================================================================
TEST(CpuUsageSample, UnknownDefault) {
    CpuUsageSample u = CpuUsageSample::unknown();
    EXPECT_FALSE(u.valid);
    EXPECT_EQ(u.core_count(), 0u);
}

TEST(NullCpuStatSource, InjectAndClear) {
    NullCpuStatSource src;
    EXPECT_FALSE(src.sample().valid);  // 預設未知
    src.set_per_core({0.1, 0.3});      // 平均 0.2
    CpuUsageSample u = src.sample();
    ASSERT_TRUE(u.valid);
    EXPECT_NEAR(u.overall, 0.2, kEps);
    EXPECT_EQ(u.core_count(), 2u);
    src.clear();
    EXPECT_FALSE(src.sample().valid);  // 回到未知
}
