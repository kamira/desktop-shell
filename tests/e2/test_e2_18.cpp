// E2-18 效能計數器（任意）— 測試（gtest）
//
// 覆蓋：提供者身分、以字串鍵註冊具名計數器→讀值、多計數器列舉、瞬時值（Instant）、
// 累積差分速率（Rate，兩次取樣差分）、查無鍵誠實 invalid、動態新增 / 移除鍵、無讀值
// invalid、經 E2-02 排程採樣（除頻）、null 來源保守、範圍 unbounded、消費者只走 E2-01
// 抽象介面、重複註冊保守拒絕、歷史累積、null 來源注入 API、CounterMode 字串。
// 相位 1：只驗介面 + 注入式來源行為，不含任何平台分支 / 真實效能計數器 API。
#include "perf_counter.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "metric.hpp"
#include "sampling.hpp"

using ds::metrics::Metric;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRegistry;
using ds::metrics::SamplingScheduler;
using ds::metrics::SamplingTier;
using ds::sysinfo::CounterMode;
using ds::sysinfo::NullPerfCounterSource;
using ds::sysinfo::PerfCounterProvider;
using ds::sysinfo::PerfCounterSource;

namespace {

constexpr double kEps = 1e-9;

// 建一個注入固定值的 null 來源。
std::shared_ptr<NullPerfCounterSource> makeSource() {
    return std::make_shared<NullPerfCounterSource>();
}

}  // namespace

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(PerfCounterProvider, ProviderIdIsStable) {
    PerfCounterProvider p{makeSource()};
    EXPECT_EQ(p.provider_id(), "sysinfo.perf");
    EXPECT_EQ(std::string(PerfCounterProvider::kMetricId), "perf.counters");
    EXPECT_EQ(std::string(PerfCounterProvider::kMetricName), "Performance Counters");
    EXPECT_EQ(std::string(PerfCounterProvider::kUnit), "");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(PerfCounterProvider, IsMetricProvider) {
    PerfCounterProvider p{makeSource()};
    MetricProvider& mp = p;  // 可上轉
    EXPECT_EQ(mp.provider_id(), "sysinfo.perf");
}

// 預設採集分級 = Normal，可由建構子覆寫。
TEST(PerfCounterProvider, SamplingTierDefaultsNormalOverridable) {
    PerfCounterProvider def{makeSource()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::Normal);

    PerfCounterProvider high{makeSource(), PerfCounterProvider::kDefaultHistory,
                             SamplingTier::High};
    EXPECT_EQ(high.sampling_tier(), SamplingTier::High);
}

// ===========================================================================
// 以字串鍵註冊具名計數器 → 讀值
// ===========================================================================
TEST(PerfCounterProvider, RegisterNamedCounterAndReadValue) {
    auto src = makeSource();
    // 任意鍵（模擬 Windows PerfCounter 路徑）。
    src->set_value("\\Processor(_Total)\\% Processor Time", 42.5);

    PerfCounterProvider p{src};
    EXPECT_TRUE(p.add_counter("\\Processor(_Total)\\% Processor Time", "CPU Time"));

    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 1u);
    auto m = reg.get("perf.counters");
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->instance_count(), 1u);

    const auto* inst = m->find_instance("\\Processor(_Total)\\% Processor Time");
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(inst->label(), "CPU Time");
    ASSERT_TRUE(inst->value().valid);
    EXPECT_NEAR(inst->value().number, 42.5, kEps);
}

// ===========================================================================
// 多計數器
// ===========================================================================
TEST(PerfCounterProvider, MultipleCountersEnumeratedInOrder) {
    auto src = makeSource();
    src->set_value("queue.len", 3.0);
    src->set_value("temp.c", 55.0);
    src->set_value("custom.key", 7.0);

    PerfCounterProvider p{src};
    p.add_counter("queue.len", "Queue Length");
    p.add_counter("temp.c", "Temperature");
    p.add_counter("custom.key", "Custom");

    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("perf.counters");
    ASSERT_EQ(m->instance_count(), 3u);
    // 列舉順序 = 註冊順序（決定性）。
    EXPECT_EQ(m->instance(0).instance_id(), "queue.len");
    EXPECT_EQ(m->instance(1).instance_id(), "temp.c");
    EXPECT_EQ(m->instance(2).instance_id(), "custom.key");
    EXPECT_NEAR(m->find_instance("temp.c")->value().number, 55.0, kEps);
}

// ===========================================================================
// 瞬時值（Instant）
// ===========================================================================
TEST(PerfCounterProvider, InstantModeExposesRawValueAndHistory) {
    auto src = makeSource();
    // 序列讀數：10, 20, 30。
    src->set_sequence("g", std::vector<double>{10.0, 20.0, 30.0});

    PerfCounterProvider p{src};
    p.add_counter("g", "Gauge", CounterMode::Instant);
    MetricRegistry reg;
    reg.add_provider(p);  // register 讀第一份 → 10
    auto m = reg.get("perf.counters");
    const auto* inst = m->find_instance("g");
    ASSERT_TRUE(inst->value().valid);
    EXPECT_NEAR(inst->value().number, 10.0, kEps);

    p.sample();  // 讀第二份 → 20
    EXPECT_NEAR(inst->value().number, 20.0, kEps);
    p.sample();  // 讀第三份 → 30
    EXPECT_NEAR(inst->value().number, 30.0, kEps);

    // 瞬時值全部入歷史：10, 20, 30。
    const auto& h = inst->history();
    ASSERT_EQ(h.size(), 3u);
    EXPECT_NEAR(h.at(0), 10.0, kEps);
    EXPECT_NEAR(h.at(2), 30.0, kEps);
}

// ===========================================================================
// 累積差分速率（Rate）
// ===========================================================================
TEST(PerfCounterProvider, RateModeDifferencesCumulativeCounter) {
    auto src = makeSource();
    // 累積計數器：100, 150, 220, 220（增量 50, 70, 0）。
    src->set_sequence("c", std::vector<double>{100.0, 150.0, 220.0, 220.0});

    PerfCounterProvider p{src};
    p.add_counter("c", "Cumulative", CounterMode::Rate);
    MetricRegistry reg;
    reg.add_provider(p);  // register 讀第一份（100）→ 設基準，未知（差分需兩份）
    auto m = reg.get("perf.counters");
    const auto* inst = m->find_instance("c");
    EXPECT_FALSE(inst->value().valid);  // 首份 → 未知

    p.sample();  // 150 - 100 = 50
    ASSERT_TRUE(inst->value().valid);
    EXPECT_NEAR(inst->value().number, 50.0, kEps);

    p.sample();  // 220 - 150 = 70
    EXPECT_NEAR(inst->value().number, 70.0, kEps);

    p.sample();  // 220 - 220 = 0（無增量）
    ASSERT_TRUE(inst->value().valid);
    EXPECT_NEAR(inst->value().number, 0.0, kEps);

    // 只有有效差分入歷史（首份未知不入）：50, 70, 0。
    const auto& h = inst->history();
    ASSERT_EQ(h.size(), 3u);
    EXPECT_NEAR(h.at(0), 50.0, kEps);
    EXPECT_NEAR(h.at(1), 70.0, kEps);
    EXPECT_NEAR(h.at(2), 0.0, kEps);
}

// Rate 計數器重置（curr < prev）→ 保守 0（不謊報負值）。
TEST(PerfCounterProvider, RateModeCounterResetIsConservativeZero) {
    auto src = makeSource();
    // 200 → 250（+50）→ 30（重置，理應保守 0）→ 90（90-30=60）。
    src->set_sequence("c", std::vector<double>{200.0, 250.0, 30.0, 90.0});

    PerfCounterProvider p{src};
    p.add_counter("c", "C", CounterMode::Rate);
    MetricRegistry reg;
    reg.add_provider(p);  // 基準 200
    auto m = reg.get("perf.counters");
    const auto* inst = m->find_instance("c");

    p.sample();  // 250-200 = 50
    EXPECT_NEAR(inst->value().number, 50.0, kEps);
    p.sample();  // 30 < 250 → 重置 → 0
    ASSERT_TRUE(inst->value().valid);
    EXPECT_NEAR(inst->value().number, 0.0, kEps);
    p.sample();  // 90 - 30 = 60（基準已推進到 30）
    EXPECT_NEAR(inst->value().number, 60.0, kEps);
}

// ===========================================================================
// 查無鍵誠實 invalid
// ===========================================================================
TEST(PerfCounterProvider, UnknownKeyIsHonestlyInvalid) {
    auto src = makeSource();
    src->set_value("present", 1.0);

    PerfCounterProvider p{src};
    // 註冊一個來源沒有的鍵。
    p.add_counter("absent", "Absent");
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("perf.counters");
    const auto* inst = m->find_instance("absent");
    ASSERT_NE(inst, nullptr);
    EXPECT_FALSE(inst->value().valid);  // 查無鍵 → 未知，不靜默當 0
    EXPECT_TRUE(inst->history().empty());
}

// ===========================================================================
// 無讀值 invalid（source 有鍵但序列耗盡為空）
// ===========================================================================
TEST(PerfCounterProvider, NoReadingIsInvalidNotSilentZero) {
    auto src = makeSource();
    src->set_sequence("k", std::vector<double>{});  // 空序列 → read 恆 nullopt

    PerfCounterProvider p{src};
    p.add_counter("k", "K", CounterMode::Instant);
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("perf.counters");
    const auto* inst = m->find_instance("k");
    EXPECT_FALSE(inst->value().valid);  // 無讀值 → 未知
    EXPECT_TRUE(inst->history().empty());

    // 之後注入有效值 → sample() 後可讀。
    src->set_value("k", 9.0);
    p.sample();
    ASSERT_TRUE(inst->value().valid);
    EXPECT_NEAR(inst->value().number, 9.0, kEps);
}

// 無讀值後再無讀值：不污染歷史，維持未知。
TEST(PerfCounterProvider, MissingReadingDoesNotPolluteHistory) {
    auto src = makeSource();
    src->set_value("k", 5.0);
    PerfCounterProvider p{src};
    p.add_counter("k", "K", CounterMode::Instant);
    MetricRegistry reg;
    reg.add_provider(p);  // 讀 5
    auto m = reg.get("perf.counters");
    const auto* inst = m->find_instance("k");
    ASSERT_EQ(inst->history().size(), 1u);

    src->remove("k");  // 此後無讀值
    p.sample();
    EXPECT_FALSE(inst->value().valid);       // 未知
    EXPECT_EQ(inst->history().size(), 1u);   // 歷史不變（不謊報 0）
}

// ===========================================================================
// 動態新增 / 移除鍵
// ===========================================================================
TEST(PerfCounterProvider, DynamicAddAfterRegister) {
    auto src = makeSource();
    src->set_value("a", 1.0);
    src->set_value("b", 2.0);

    PerfCounterProvider p{src};
    p.add_counter("a", "A");
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("perf.counters");
    EXPECT_EQ(m->instance_count(), 1u);

    // register 之後動態新增 → 立即掛上實例並讀初值。
    EXPECT_TRUE(p.add_counter("b", "B"));
    EXPECT_EQ(p.counter_count(), 2u);
    EXPECT_EQ(m->instance_count(), 2u);
    const auto* b = m->find_instance("b");
    ASSERT_NE(b, nullptr);
    ASSERT_TRUE(b->value().valid);
    EXPECT_NEAR(b->value().number, 2.0, kEps);
}

TEST(PerfCounterProvider, DynamicRemoveAfterRegister) {
    auto src = makeSource();
    src->set_value("a", 1.0);
    src->set_value("b", 2.0);

    PerfCounterProvider p{src};
    p.add_counter("a", "A");
    p.add_counter("b", "B");
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("perf.counters");
    ASSERT_EQ(m->instance_count(), 2u);

    // 動態移除 "a" → 實例數降為 1，剩 "b"。
    EXPECT_TRUE(p.remove_counter("a"));
    EXPECT_EQ(p.counter_count(), 1u);
    EXPECT_EQ(m->instance_count(), 1u);
    EXPECT_EQ(m->find_instance("a"), nullptr);
    ASSERT_NE(m->find_instance("b"), nullptr);
    EXPECT_EQ(m->instance(0).instance_id(), "b");

    // 移除不存在的鍵 → false。
    EXPECT_FALSE(p.remove_counter("zzz"));

    // sample() 在移除後仍正確（只更新剩餘實例，不崩）。
    src->set_value("b", 20.0);
    p.sample();
    EXPECT_NEAR(m->find_instance("b")->value().number, 20.0, kEps);
}

// register 前新增 / 移除（暫存佇列）。
TEST(PerfCounterProvider, AddRemoveBeforeRegister) {
    auto src = makeSource();
    src->set_value("x", 1.0);
    PerfCounterProvider p{src};
    p.add_counter("x", "X");
    p.add_counter("y", "Y");
    EXPECT_EQ(p.counter_count(), 2u);
    EXPECT_TRUE(p.remove_counter("y"));  // 移除暫存佇列中的 y
    EXPECT_EQ(p.counter_count(), 1u);

    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("perf.counters");
    EXPECT_EQ(m->instance_count(), 1u);
    EXPECT_TRUE(p.tracks_counter("x"));
    EXPECT_FALSE(p.tracks_counter("y"));
}

// 空鍵 / 重複鍵註冊保守拒絕。
TEST(PerfCounterProvider, EmptyAndDuplicateKeyRejected) {
    PerfCounterProvider p{makeSource()};
    EXPECT_FALSE(p.add_counter("", "Empty"));  // 空鍵無效
    EXPECT_TRUE(p.add_counter("k", "K"));
    EXPECT_FALSE(p.add_counter("k", "K again"));  // 重複 → 拒絕
    EXPECT_EQ(p.counter_count(), 1u);
}

// ===========================================================================
// 經 E2-02 採樣（除頻排程）
// ===========================================================================
TEST(PerfCounterProvider, SampledViaE2_02Scheduler) {
    auto src = makeSource();
    // 累積計數器每次 +10。
    std::vector<double> seq;
    for (int k = 0; k <= 20; ++k) seq.push_back(10.0 * k);
    src->set_sequence("c", std::move(seq));

    PerfCounterProvider p{src};
    p.add_counter("c", "C", CounterMode::Rate);
    MetricRegistry reg;
    reg.add_provider(p);  // register 消耗第 0 份（基準 0）

    SamplingScheduler sched;  // 預設 Normal 間隔=8
    sched.add_demand(PerfCounterProvider::kMetricId, p.sampling_tier());
    ASSERT_TRUE(sched.effective_tier(PerfCounterProvider::kMetricId).has_value());
    EXPECT_EQ(*sched.effective_tier(PerfCounterProvider::kMetricId), SamplingTier::Normal);

    // 推進 32 個 tick，Normal 間隔 8 → 於 t=8,16,24,32 到期（4 次）。
    int sampled = 0;
    for (ds::metrics::Tick t = 1; t <= 32; ++t) {
        auto due = sched.advance(t);
        for (const auto& id : due) {
            if (id == PerfCounterProvider::kMetricId) {
                p.sample();
                ++sampled;
            }
        }
    }
    EXPECT_EQ(sampled, 4);

    auto m = reg.get("perf.counters");
    const auto& h = m->find_instance("c")->history();
    // 4 次差分 → 歷史 4 筆，各 +10。
    EXPECT_EQ(h.size(), 4u);
    EXPECT_NEAR(h.latest(), 10.0, kEps);
}

// 除頻：多消費者同一指標合併，最高頻者供給。
TEST(PerfCounterProvider, DeFrequencyCoalescesDemands) {
    PerfCounterProvider p{makeSource()};
    SamplingScheduler sched;
    auto d_low = sched.add_demand(PerfCounterProvider::kMetricId, SamplingTier::Low);
    sched.add_demand(PerfCounterProvider::kMetricId, SamplingTier::High);
    EXPECT_EQ(*sched.effective_tier(PerfCounterProvider::kMetricId), SamplingTier::High);
    EXPECT_EQ(sched.demand_count(PerfCounterProvider::kMetricId), 2u);

    EXPECT_TRUE(sched.remove_demand(d_low));
    EXPECT_TRUE(sched.tracks(PerfCounterProvider::kMetricId));
    EXPECT_EQ(*sched.effective_tier(PerfCounterProvider::kMetricId), SamplingTier::High);
}

// ===========================================================================
// null 來源行為
// ===========================================================================
TEST(PerfCounterProvider, NullSourceIsConservative) {
    // source 為 null 指標：仍掛上指標，計數器實例未知、不崩。
    PerfCounterProvider p{nullptr};
    p.add_counter("k", "K");
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 1u);
    auto m = reg.get("perf.counters");
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->instance_count(), 1u);
    EXPECT_FALSE(m->find_instance("k")->value().valid);  // 無來源 → 未知
    EXPECT_TRUE(p.available_keys().empty());
    // sample() 不崩。
    p.sample();
    EXPECT_FALSE(m->find_instance("k")->value().valid);
}

// register 前無計數器 → 掛上空指標，不崩。
TEST(PerfCounterProvider, EmptyProviderRegistersEmptyMetric) {
    PerfCounterProvider p{makeSource()};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 1u);
    auto m = reg.get("perf.counters");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->instance_count(), 0u);
    p.sample();  // no-op，不崩
}

// sample() 未 register_metrics 時為 no-op（不崩）。
TEST(PerfCounterProvider, SampleBeforeRegisterIsNoop) {
    auto src = makeSource();
    src->set_value("k", 1.0);
    PerfCounterProvider p{src};
    p.add_counter("k", "K");
    p.sample();  // 尚未 register → no-op，不崩
    EXPECT_EQ(p.counter_count(), 1u);
}

// ===========================================================================
// 範圍 / 消費者範式 / 重複註冊
// ===========================================================================
TEST(PerfCounterProvider, RangeIsUnbounded) {
    PerfCounterProvider p{makeSource()};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("perf.counters");
    auto r = m->range();
    EXPECT_FALSE(r.is_bounded());
    EXPECT_FALSE(r.has_min());
    EXPECT_FALSE(r.has_max());
}

// 掛件風格消費者：只透過 E2-01 registry / Metric 抽象介面走訪，完全不觸及具體型別。
TEST(PerfCounterProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = makeSource();
    src->set_value("a", 10.0);
    src->set_value("b", 30.0);
    PerfCounterProvider p{src};
    p.add_counter("a", "A");
    p.add_counter("b", "B");
    MetricRegistry reg;
    reg.add_provider(p);

    std::shared_ptr<Metric> m = reg.get("perf.counters");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), "");
    double sum = 0.0;
    for (std::size_t i = 0; i < m->instance_count(); ++i) {
        const auto& inst = m->instance(i);
        if (inst.value().valid) sum += inst.value().number;
    }
    EXPECT_NEAR(sum, 40.0, kEps);  // 10 + 30
}

TEST(PerfCounterProvider, DuplicateRegistrationRejected) {
    PerfCounterProvider p1{makeSource()};
    PerfCounterProvider p2{makeSource()};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 1u);
    // 第二個提供者掛同一 id → 保守拒絕（不覆寫既有）。
    EXPECT_EQ(reg.add_provider(p2), 0u);
    EXPECT_EQ(reg.size(), 1u);
}

// ===========================================================================
// NullPerfCounterSource 單元
// ===========================================================================
TEST(NullPerfCounterSource, FixedValueDoesNotAdvance) {
    NullPerfCounterSource src;
    EXPECT_FALSE(src.read("k").has_value());  // 查無鍵 → nullopt
    src.set_value("k", 7.0);
    EXPECT_NEAR(*src.read("k"), 7.0, kEps);
    EXPECT_NEAR(*src.read("k"), 7.0, kEps);  // 固定值不推進
    EXPECT_TRUE(src.has("k"));
    EXPECT_EQ(src.size(), 1u);
}

TEST(NullPerfCounterSource, SequenceAdvancesThenHoldsLast) {
    NullPerfCounterSource src;
    src.set_sequence("s", std::vector<double>{1.0, 2.0, 3.0});
    EXPECT_NEAR(*src.read("s"), 1.0, kEps);
    EXPECT_NEAR(*src.read("s"), 2.0, kEps);
    EXPECT_NEAR(*src.read("s"), 3.0, kEps);
    EXPECT_NEAR(*src.read("s"), 3.0, kEps);  // 列盡 → 持續回最後一份
}

TEST(NullPerfCounterSource, RemoveClearAndAvailableKeys) {
    NullPerfCounterSource src;
    src.set_value("a", 1.0);
    src.set_value("b", 2.0);
    src.set_value("c", 3.0);
    // available_keys 依注入順序（決定性）。
    std::vector<std::string> keys = src.available_keys();
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], "a");
    EXPECT_EQ(keys[2], "c");

    EXPECT_TRUE(src.remove("b"));
    EXPECT_FALSE(src.remove("b"));  // 已移除
    EXPECT_FALSE(src.has("b"));
    EXPECT_FALSE(src.read("b").has_value());

    src.clear();
    EXPECT_TRUE(src.empty());
    EXPECT_TRUE(src.available_keys().empty());
}

TEST(NullPerfCounterSource, EmptySequenceReadsNullopt) {
    NullPerfCounterSource src;
    src.set_sequence("e", std::vector<double>{});
    EXPECT_TRUE(src.has("e"));
    EXPECT_FALSE(src.read("e").has_value());  // 空序列 → 無讀值
}

// provider.available_keys 轉發來源。
TEST(PerfCounterProvider, AvailableKeysForwardsSource) {
    auto src = makeSource();
    src->set_value("k1", 1.0);
    src->set_value("k2", 2.0);
    PerfCounterProvider p{src};
    auto keys = p.available_keys();
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0], "k1");
    EXPECT_EQ(keys[1], "k2");
}

// ===========================================================================
// CounterMode 字串
// ===========================================================================
TEST(CounterMode, ToStringStable) {
    EXPECT_EQ(std::string(ds::sysinfo::to_string(CounterMode::Instant)), "instant");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(CounterMode::Rate)), "rate");
}
