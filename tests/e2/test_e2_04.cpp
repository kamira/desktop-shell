// E2-04 記憶體使用量（實體/交換）— 測試（gtest）
//
// 覆蓋：提供者身分、註冊到 E2-01 registry、固定欄位集列舉（實體總量/已用/可用/使用率 %
// + swap 總量/已用）、實體記憶體各欄位讀值、使用率 % 計算、swap 欄位、0% / 100% / 空 swap
// 邊界、無讀值 invalid（各欄位未知）、total==0 時使用率未知、經 E2-02 採樣（除頻排程 + 歷史
// 累積）、null 來源（source 為 null / 空序列預設未知）、序列注入（時間推進用量變化）、
// 消費者只走 E2-01 抽象介面、重複註冊保守拒絕、值模型單元。
// 相位 1：只驗介面 + 注入式來源行為，不含任何平台分支 / 真實記憶體 API。
#include "memory_usage.hpp"

#include <gtest/gtest.h>

#include <cstdint>
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
using ds::sysinfo::MemoryProvider;
using ds::sysinfo::MemoryStats;
using ds::sysinfo::MemoryStatSource;
using ds::sysinfo::NullMemoryStatSource;

namespace {

constexpr double kEps = 1e-6;
constexpr std::uint64_t kGiB = 1024ull * 1024ull * 1024ull;

// 固定值來源便利建構。
std::shared_ptr<NullMemoryStatSource> makeFixed(MemoryStats s) {
    return std::make_shared<NullMemoryStatSource>(std::move(s));
}

// 讀某欄位值（已註冊指標）。MetricInstance::value() 回值（by value），故此處亦回值。
ds::metrics::MetricValue fieldValue(const std::shared_ptr<Metric>& m, const char* key) {
    const auto* inst = m->find_instance(key);
    return inst->value();
}

}  // namespace

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(MemoryProvider, ProviderIdAndMetricIdentityStable) {
    MemoryProvider p{std::make_shared<NullMemoryStatSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.memory");
    EXPECT_EQ(std::string(MemoryProvider::kMetricId), "memory.stats");
    EXPECT_EQ(std::string(MemoryProvider::kMetricName), "Memory");
    EXPECT_EQ(std::string(MemoryProvider::kProviderId), "sysinfo.memory");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(MemoryProvider, IsMetricProvider) {
    MemoryProvider p{std::make_shared<NullMemoryStatSource>()};
    MetricProvider& mp = p;  // 可上轉
    EXPECT_EQ(mp.provider_id(), "sysinfo.memory");
}

// 預設採集分級 = Normal（記憶體用量屬常規頻率），可由建構子覆寫。
TEST(MemoryProvider, SamplingTierDefaultsNormalOverridable) {
    MemoryProvider def{std::make_shared<NullMemoryStatSource>()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::Normal);

    MemoryProvider low{std::make_shared<NullMemoryStatSource>(),
                       MemoryProvider::kDefaultHistory, SamplingTier::Low};
    EXPECT_EQ(low.sampling_tier(), SamplingTier::Low);
}

// ===========================================================================
// 註冊 / 列舉：固定欄位集
// ===========================================================================
TEST(MemoryProvider, RegistersSingleMetricWithFixedFieldSet) {
    auto src = makeFixed(MemoryStats::from_physical(8 * kGiB, 4 * kGiB, 2 * kGiB, kGiB));
    MemoryProvider p{src};
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);

    EXPECT_EQ(added, 1u);
    EXPECT_TRUE(reg.contains("memory.stats"));
    auto m = reg.get("memory.stats");
    ASSERT_NE(m, nullptr);

    // 固定 6 欄位，列舉順序決定性。
    ASSERT_EQ(m->instance_count(), 6u);
    EXPECT_EQ(m->instance(0).instance_id(), "memory.physical.total");
    EXPECT_EQ(m->instance(0).label(), "Physical Total");
    EXPECT_EQ(m->instance(1).instance_id(), "memory.physical.used");
    EXPECT_EQ(m->instance(2).instance_id(), "memory.physical.available");
    EXPECT_EQ(m->instance(3).instance_id(), "memory.usage.percent");
    EXPECT_EQ(m->instance(3).label(), "Memory Usage");
    EXPECT_EQ(m->instance(4).instance_id(), "memory.swap.total");
    EXPECT_EQ(m->instance(5).instance_id(), "memory.swap.used");
    // metric 層 unit=""（欄位異質），range unbounded。
    EXPECT_EQ(m->unit(), "");
    EXPECT_FALSE(m->range().is_bounded());
}

// ===========================================================================
// 實體記憶體各欄位讀值 + 使用率計算
// ===========================================================================
TEST(MemoryProvider, PhysicalFieldsAndUsagePercent) {
    // total=8G, used=6G, available=2G → 使用率 75%。
    auto src = makeFixed(MemoryStats::from_physical(8 * kGiB, 6 * kGiB));
    MemoryProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("memory.stats");
    ASSERT_NE(m, nullptr);

    const auto& total = fieldValue(m, MemoryProvider::kFieldPhysicalTotal);
    ASSERT_TRUE(total.valid);
    EXPECT_NEAR(total.number, static_cast<double>(8 * kGiB), 1.0);

    const auto& used = fieldValue(m, MemoryProvider::kFieldPhysicalUsed);
    ASSERT_TRUE(used.valid);
    EXPECT_NEAR(used.number, static_cast<double>(6 * kGiB), 1.0);

    // from_physical 推導 available = total - used = 2G。
    const auto& avail = fieldValue(m, MemoryProvider::kFieldPhysicalAvailable);
    ASSERT_TRUE(avail.valid);
    EXPECT_NEAR(avail.number, static_cast<double>(2 * kGiB), 1.0);

    const auto& usage = fieldValue(m, MemoryProvider::kFieldUsagePercent);
    ASSERT_TRUE(usage.valid);
    EXPECT_NEAR(usage.number, 75.0, kEps);
}

// available 由來源獨立給出（非必然 total-used；誠實承載）。
TEST(MemoryProvider, AvailableCarriedFromSourceIndependently) {
    // 直接設欄位：total=10G, used=4G, available=5G（buffer/cache 使 used+avail != total）。
    MemoryStats s;
    s.physical_total = 10 * kGiB;
    s.physical_used = 4 * kGiB;
    s.physical_available = 5 * kGiB;
    s.valid = true;
    MemoryProvider p{makeFixed(s)};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("memory.stats");
    EXPECT_NEAR(fieldValue(m, MemoryProvider::kFieldPhysicalAvailable).number,
                static_cast<double>(5 * kGiB), 1.0);
    // 使用率仍由 used/total = 40%。
    EXPECT_NEAR(fieldValue(m, MemoryProvider::kFieldUsagePercent).number, 40.0, kEps);
}

// ===========================================================================
// swap 欄位
// ===========================================================================
TEST(MemoryProvider, SwapFields) {
    auto src = makeFixed(MemoryStats::from_physical(8 * kGiB, 4 * kGiB, 4 * kGiB, kGiB));
    MemoryProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("memory.stats");

    const auto& st = fieldValue(m, MemoryProvider::kFieldSwapTotal);
    const auto& su = fieldValue(m, MemoryProvider::kFieldSwapUsed);
    ASSERT_TRUE(st.valid);
    ASSERT_TRUE(su.valid);
    EXPECT_NEAR(st.number, static_cast<double>(4 * kGiB), 1.0);
    EXPECT_NEAR(su.number, static_cast<double>(kGiB), 1.0);
}

// ===========================================================================
// 0% / 100% / 空 swap 邊界
// ===========================================================================
TEST(MemoryStatsValueModel, UsagePercentBoundaries) {
    // 0%：used=0。
    EXPECT_NEAR(MemoryStats::from_physical(8 * kGiB, 0).usage_percent(), 0.0, kEps);
    // 100%：used==total。
    EXPECT_NEAR(MemoryStats::from_physical(8 * kGiB, 8 * kGiB).usage_percent(), 100.0, kEps);
    // used > total（理論不該發生）→ 夾到 100%。
    MemoryStats over;
    over.physical_total = kGiB;
    over.physical_used = 2 * kGiB;
    over.valid = true;
    EXPECT_NEAR(over.usage_percent(), 100.0, kEps);
    // total==0 → 0（不謊報；有效性另判）。
    EXPECT_NEAR(MemoryStats::unknown().usage_percent(), 0.0, kEps);
}

TEST(MemoryProvider, ZeroAndFullUsageThroughProvider) {
    {  // 0%
        MemoryProvider p{makeFixed(MemoryStats::from_physical(8 * kGiB, 0))};
        MetricRegistry reg;
        reg.add_provider(p);
        auto m = reg.get("memory.stats");
        EXPECT_NEAR(fieldValue(m, MemoryProvider::kFieldUsagePercent).number, 0.0, kEps);
    }
    {  // 100%
        MemoryProvider p{makeFixed(MemoryStats::from_physical(8 * kGiB, 8 * kGiB))};
        MetricRegistry reg;
        reg.add_provider(p);
        auto m = reg.get("memory.stats");
        EXPECT_NEAR(fieldValue(m, MemoryProvider::kFieldUsagePercent).number, 100.0, kEps);
    }
}

// 空 swap（swap_total==0）：swap 欄位仍為有效讀值 0；swap 使用率 0。
TEST(MemoryProvider, EmptySwapIsValidZero) {
    auto src = makeFixed(MemoryStats::from_physical(8 * kGiB, 4 * kGiB, 0, 0));
    MemoryProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("memory.stats");
    const auto& st = fieldValue(m, MemoryProvider::kFieldSwapTotal);
    const auto& su = fieldValue(m, MemoryProvider::kFieldSwapUsed);
    ASSERT_TRUE(st.valid);
    ASSERT_TRUE(su.valid);
    EXPECT_NEAR(st.number, 0.0, kEps);
    EXPECT_NEAR(su.number, 0.0, kEps);
    // 值模型：swap 使用率保護除零。
    EXPECT_NEAR(MemoryStats::from_physical(8 * kGiB, 4 * kGiB, 0, 0).swap_usage_percent(), 0.0,
                kEps);
    // 有 swap 時：swap_used/swap_total。
    EXPECT_NEAR(MemoryStats::from_physical(0, 0, 4 * kGiB, kGiB).swap_usage_percent(), 25.0,
                kEps);
}

// ===========================================================================
// 無讀值 invalid：各欄位誠實表達未知
// ===========================================================================
TEST(MemoryProvider, NoReadingAllFieldsInvalid) {
    // 空序列來源 → 每次 read() 回 unknown。
    MemoryProvider p{std::make_shared<NullMemoryStatSource>()};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 1u);
    auto m = reg.get("memory.stats");
    ASSERT_NE(m, nullptr);
    // 指標仍掛上、6 欄位仍在，但各欄位 valid==false（未知），非塞假 0。
    ASSERT_EQ(m->instance_count(), 6u);
    EXPECT_FALSE(fieldValue(m, MemoryProvider::kFieldPhysicalTotal).valid);
    EXPECT_FALSE(fieldValue(m, MemoryProvider::kFieldPhysicalUsed).valid);
    EXPECT_FALSE(fieldValue(m, MemoryProvider::kFieldPhysicalAvailable).valid);
    EXPECT_FALSE(fieldValue(m, MemoryProvider::kFieldUsagePercent).valid);
    EXPECT_FALSE(fieldValue(m, MemoryProvider::kFieldSwapTotal).valid);
    EXPECT_FALSE(fieldValue(m, MemoryProvider::kFieldSwapUsed).valid);
}

// total==0（有讀值但無法算使用率）→ 使用率欄位未知；bytes 欄位仍有效。
TEST(MemoryProvider, ZeroTotalMarksUsageUnknownButBytesValid) {
    MemoryStats s;
    s.valid = true;  // 有讀值但 total==0（未知硬體）
    MemoryProvider p{makeFixed(s)};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("memory.stats");
    // bytes 欄位有效（讀值 0 是誠實的）。
    EXPECT_TRUE(fieldValue(m, MemoryProvider::kFieldPhysicalTotal).valid);
    // 使用率無從判斷 → 未知（不謊報 0%）。
    EXPECT_FALSE(fieldValue(m, MemoryProvider::kFieldUsagePercent).valid);
}

// ===========================================================================
// 經 E2-02 採樣：除頻排程 + 歷史累積
// ===========================================================================
TEST(MemoryProvider, SampledViaE2_02SchedulerAccumulatesHistory) {
    // 一列快照：used 隨時間爬升（每份 +1G），total 固定 8G。
    std::vector<MemoryStats> seq;
    for (std::uint64_t k = 0; k <= 10; ++k) {
        seq.push_back(MemoryStats::from_physical(8 * kGiB, k * kGiB));
    }
    auto src = std::make_shared<NullMemoryStatSource>(std::move(seq));
    MemoryProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // register 消耗第 0 份（used=0 → 0%）

    SamplingScheduler sched;  // 預設 policy：Normal 間隔=8
    sched.add_demand(MemoryProvider::kMetricId, p.sampling_tier());
    ASSERT_TRUE(sched.effective_tier(MemoryProvider::kMetricId).has_value());
    EXPECT_EQ(*sched.effective_tier(MemoryProvider::kMetricId), SamplingTier::Normal);

    // 推進到多個 Normal 間隔（8/16/24），每到期呼叫 provider.sample()。
    int sampled = 0;
    for (ds::metrics::Tick t : {8u, 16u, 24u}) {
        auto due = sched.advance(t);
        for (const auto& id : due) {
            if (id == MemoryProvider::kMetricId) {
                p.sample();
                ++sampled;
            }
        }
    }
    EXPECT_EQ(sampled, 3);

    auto m = reg.get("memory.stats");
    // 使用率歷史：register(0%) + 3 次採樣（used=1G/2G/3G → 12.5/25/37.5%）= 4 筆。
    const auto& hist = m->find_instance(MemoryProvider::kFieldUsagePercent)->history();
    EXPECT_EQ(hist.size(), 4u);
    EXPECT_NEAR(hist.latest(), 37.5, kEps);
    // 目前使用率欄位讀值一致。
    EXPECT_NEAR(fieldValue(m, MemoryProvider::kFieldUsagePercent).number, 37.5, kEps);
}

// 除頻：多消費者同一指標合併，最高頻者供給。
TEST(MemoryProvider, DeFrequencyCoalescesDemands) {
    MemoryProvider p{makeFixed(MemoryStats::from_physical(8 * kGiB, 4 * kGiB))};
    SamplingScheduler sched;
    auto d_low = sched.add_demand(MemoryProvider::kMetricId, SamplingTier::Low);
    sched.add_demand(MemoryProvider::kMetricId, SamplingTier::High);
    ASSERT_TRUE(sched.effective_tier(MemoryProvider::kMetricId).has_value());
    EXPECT_EQ(*sched.effective_tier(MemoryProvider::kMetricId), SamplingTier::High);
    EXPECT_EQ(sched.demand_count(MemoryProvider::kMetricId), 2u);

    EXPECT_TRUE(sched.remove_demand(d_low));
    EXPECT_TRUE(sched.tracks(MemoryProvider::kMetricId));
    EXPECT_EQ(*sched.effective_tier(MemoryProvider::kMetricId), SamplingTier::High);
}

// 靜態欄位（total）無歷史（capacity==0）；變動欄位（used）有歷史。
TEST(MemoryProvider, StaticFieldsHaveNoHistoryVaryingDo) {
    auto src = makeFixed(MemoryStats::from_physical(8 * kGiB, 4 * kGiB));
    MemoryProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("memory.stats");
    // total 欄位 history capacity==0（靜態）。
    EXPECT_EQ(m->find_instance(MemoryProvider::kFieldPhysicalTotal)->history().capacity(), 0u);
    EXPECT_EQ(m->find_instance(MemoryProvider::kFieldSwapTotal)->history().capacity(), 0u);
    // used 欄位有歷史容量。
    EXPECT_GT(m->find_instance(MemoryProvider::kFieldPhysicalUsed)->history().capacity(), 0u);
    EXPECT_GT(m->find_instance(MemoryProvider::kFieldUsagePercent)->history().capacity(), 0u);
}

// ===========================================================================
// null 來源行為
// ===========================================================================
TEST(MemoryProvider, NullSourcePointerIsConservative) {
    // source 為 null 指標：仍掛上指標，各欄位未知，不崩。
    MemoryProvider p{nullptr};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 1u);
    auto m = reg.get("memory.stats");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->instance_count(), 6u);
    EXPECT_FALSE(fieldValue(m, MemoryProvider::kFieldPhysicalTotal).valid);
    // sample() 不崩、仍未知。
    p.sample();
    EXPECT_FALSE(fieldValue(m, MemoryProvider::kFieldUsagePercent).valid);
}

// sample() 未 register_metrics 時為 no-op（不崩）。
TEST(MemoryProvider, SampleBeforeRegisterIsNoop) {
    MemoryProvider p{makeFixed(MemoryStats::from_physical(8 * kGiB, 4 * kGiB))};
    p.sample();  // 尚未 register → no-op，不崩
    SUCCEED();
}

// NullMemoryStatSource 預設（空序列）→ 無讀值；注入後可讀。
TEST(NullMemoryStatSource, DefaultUnknownThenInjected) {
    NullMemoryStatSource src;
    EXPECT_TRUE(src.empty());
    EXPECT_FALSE(src.read().valid);  // 空列 → 未知

    src.set_stats(MemoryStats::from_physical(8 * kGiB, 2 * kGiB));
    MemoryStats s = src.read();
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.physical_total, 8 * kGiB);
    EXPECT_EQ(s.physical_used, 2 * kGiB);

    src.clear();
    EXPECT_TRUE(src.empty());
    EXPECT_FALSE(src.read().valid);  // 回到未知
}

// 序列注入：時間推進下用量變化；列盡持續回最後一份。
TEST(NullMemoryStatSource, SequenceAdvancesThenHoldsLast) {
    NullMemoryStatSource src{std::vector<MemoryStats>{
        MemoryStats::from_physical(8 * kGiB, 1 * kGiB),
        MemoryStats::from_physical(8 * kGiB, 2 * kGiB),
    }};
    EXPECT_EQ(src.size(), 2u);
    EXPECT_EQ(src.read().physical_used, 1 * kGiB);  // 第 0 份
    EXPECT_EQ(src.read().physical_used, 2 * kGiB);  // 第 1 份
    EXPECT_EQ(src.read().physical_used, 2 * kGiB);  // 列盡 → 持續回最後一份

    src.reset();
    EXPECT_EQ(src.read().physical_used, 1 * kGiB);  // 游標回起點

    src.push_sample(MemoryStats::from_physical(8 * kGiB, 5 * kGiB));
    EXPECT_EQ(src.size(), 3u);
}

// 提供者經序列來源，sample() 反映用量變化。
TEST(MemoryProvider, SampleReflectsSequenceChange) {
    auto src = std::make_shared<NullMemoryStatSource>(std::vector<MemoryStats>{
        MemoryStats::from_physical(8 * kGiB, 2 * kGiB),  // register 消耗：25%
        MemoryStats::from_physical(8 * kGiB, 6 * kGiB),  // sample：75%
    });
    MemoryProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("memory.stats");
    EXPECT_NEAR(fieldValue(m, MemoryProvider::kFieldUsagePercent).number, 25.0, kEps);
    p.sample();
    EXPECT_NEAR(fieldValue(m, MemoryProvider::kFieldUsagePercent).number, 75.0, kEps);
}

// ===========================================================================
// 消費者範式 / 重複註冊
// ===========================================================================
// 掛件風格消費者：只透過 E2-01 registry / Metric 抽象介面走訪，完全不觸及具體型別。
TEST(MemoryProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = makeFixed(MemoryStats::from_physical(16 * kGiB, 12 * kGiB));
    MemoryProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    std::shared_ptr<Metric> m = reg.get("memory.stats");
    ASSERT_NE(m, nullptr);
    // 消費者只認識 MetricRegistry + Metric + MetricInstance：走訪找使用率欄位。
    bool found = false;
    for (std::size_t i = 0; i < m->instance_count(); ++i) {
        const auto& inst = m->instance(i);
        if (inst.instance_id() == "memory.usage.percent") {
            ASSERT_TRUE(inst.value().valid);
            EXPECT_NEAR(inst.value().number, 75.0, kEps);  // 12/16 = 75%
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(MemoryProvider, DuplicateRegistrationRejected) {
    MemoryProvider p1{makeFixed(MemoryStats::from_physical(8 * kGiB, 4 * kGiB))};
    MemoryProvider p2{makeFixed(MemoryStats::from_physical(8 * kGiB, 7 * kGiB))};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 1u);
    // 第二個提供者掛同一 id → 保守拒絕（不覆寫既有）。
    EXPECT_EQ(reg.add_provider(p2), 0u);
    EXPECT_EQ(reg.size(), 1u);
}

// ===========================================================================
// 值模型單元
// ===========================================================================
TEST(MemoryStatsValueModel, UnknownDefaultAndEquality) {
    MemoryStats u = MemoryStats::unknown();
    EXPECT_FALSE(u.valid);
    EXPECT_EQ(u.physical_total, 0u);

    MemoryStats a = MemoryStats::from_physical(8 * kGiB, 4 * kGiB);
    MemoryStats b = MemoryStats::from_physical(8 * kGiB, 4 * kGiB);
    EXPECT_EQ(a, b);
    b.swap_used = kGiB;
    EXPECT_NE(a, b);
}

TEST(MemoryStatsValueModel, FromPhysicalDerivesAvailableClamped) {
    // used <= total：available = total - used。
    MemoryStats s = MemoryStats::from_physical(8 * kGiB, 3 * kGiB);
    EXPECT_EQ(s.physical_available, 5 * kGiB);
    EXPECT_TRUE(s.valid);
    // used > total（防護）：available 夾到 0。
    MemoryStats over = MemoryStats::from_physical(kGiB, 2 * kGiB);
    EXPECT_EQ(over.physical_available, 0u);
}
