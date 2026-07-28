// E2-05 GPU 使用率 / VRAM / 溫度 — 測試（gtest）
//
// 覆蓋：提供者身分、註冊到 E2-01 registry（三個指標）、每 GPU 實例列舉、
// 使用率 / VRAM / 溫度各欄位讀值正確、多 GPU、0% / 100% 邊界、GPU 數變動（增 / 減）、
// 無讀值誠實 invalid（整張 / 逐欄 / 總量 0）、經 E2-02 頻率採樣（除頻排程）、
// null 來源（固定 / 序列 / 空）、範圍（usage/vram bounded[0,100]、temp at_least(0)）、
// VRAM 已用/總量文字表述、消費者只走 E2-01 抽象介面、重複註冊保守拒絕、自由函式單元。
// 相位 1：只驗介面 + 注入式來源行為，不含任何平台分支。
#include "gpu_stats.hpp"

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
using ds::sysinfo::GpuStat;
using ds::sysinfo::GpuStatSample;
using ds::sysinfo::GpuStatsProvider;
using ds::sysinfo::GpuStatSource;
using ds::sysinfo::NullGpuStatSource;
using ds::sysinfo::humanize_bytes;
using ds::sysinfo::vram_ratio;

namespace {

constexpr double kEps = 1e-9;
constexpr std::uint64_t kGiB = 1024ULL * 1024ULL * 1024ULL;

// 固定來源：一列 GpuStat 為單份快照。
std::shared_ptr<NullGpuStatSource> makeSource(std::vector<GpuStat> gpus) {
    auto src = std::make_shared<NullGpuStatSource>();
    src->set_gpus(std::move(gpus));
    return src;
}

// 一份含 N 張 GPU 的快照。
GpuStatSample makeSample(std::vector<GpuStat> gpus) {
    GpuStatSample s;
    s.gpus = std::move(gpus);
    return s;
}

}  // namespace

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(GpuStatsProvider, ProviderIdAndMetricIdsStable) {
    GpuStatsProvider p{std::make_shared<NullGpuStatSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.gpu");
    EXPECT_EQ(std::string(GpuStatsProvider::kUsageId), "gpu.usage");
    EXPECT_EQ(std::string(GpuStatsProvider::kVramId), "gpu.vram");
    EXPECT_EQ(std::string(GpuStatsProvider::kTempId), "gpu.temp");
    EXPECT_EQ(std::string(GpuStatsProvider::kUsageName), "GPU Usage");
    EXPECT_EQ(std::string(GpuStatsProvider::kVramName), "GPU VRAM");
    EXPECT_EQ(std::string(GpuStatsProvider::kTempName), "GPU Temperature");
    EXPECT_EQ(std::string(GpuStatsProvider::kPercentUnit), "%");
    EXPECT_EQ(std::string(GpuStatsProvider::kTempUnit), "\xC2\xB0" "C");  // "°C"
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(GpuStatsProvider, IsMetricProvider) {
    GpuStatsProvider p{std::make_shared<NullGpuStatSource>()};
    MetricProvider& mp = p;  // 可上轉
    EXPECT_EQ(mp.provider_id(), "sysinfo.gpu");
}

// 預設採集分級 = Normal，可由建構子覆寫。
TEST(GpuStatsProvider, SamplingTierDefaultsNormalOverridable) {
    GpuStatsProvider def{std::make_shared<NullGpuStatSource>()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::Normal);

    GpuStatsProvider high{std::make_shared<NullGpuStatSource>(),
                          GpuStatsProvider::kDefaultHistory, SamplingTier::High};
    EXPECT_EQ(high.sampling_tier(), SamplingTier::High);
}

// ===========================================================================
// 註冊 / 列舉：三個指標、每 GPU 實例
// ===========================================================================
TEST(GpuStatsProvider, RegistersThreeMetrics) {
    auto src = makeSource({GpuStat::of(0.5, 4 * kGiB, 8 * kGiB, 65.0)});
    GpuStatsProvider p{src};
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);

    EXPECT_EQ(added, 3u);
    EXPECT_TRUE(reg.contains("gpu.usage"));
    EXPECT_TRUE(reg.contains("gpu.vram"));
    EXPECT_TRUE(reg.contains("gpu.temp"));
    EXPECT_EQ(p.gpu_count(), 1u);

    // 每個指標各有 1 個 GPU 實例，id / label 一致。
    for (const char* id : {"gpu.usage", "gpu.vram", "gpu.temp"}) {
        auto m = reg.get(id);
        ASSERT_NE(m, nullptr);
        ASSERT_EQ(m->instance_count(), 1u);
        EXPECT_EQ(m->instance(0).instance_id(), "gpu0");
        EXPECT_EQ(m->instance(0).label(), "GPU 0");
    }
}

// 使用率 / VRAM / 溫度各欄位讀值正確。
TEST(GpuStatsProvider, UsageVramTempFieldValues) {
    // 使用率 0.75 → 75%；VRAM 4/8 GiB → 50%；溫度 65°C。
    auto src = makeSource({GpuStat::of(0.75, 4 * kGiB, 8 * kGiB, 65.0)});
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    const auto* u = reg.get("gpu.usage")->find_instance("gpu0");
    ASSERT_NE(u, nullptr);
    ASSERT_TRUE(u->value().valid);
    EXPECT_NEAR(u->value().number, 75.0, kEps);

    const auto* v = reg.get("gpu.vram")->find_instance("gpu0");
    ASSERT_NE(v, nullptr);
    ASSERT_TRUE(v->value().valid);
    EXPECT_NEAR(v->value().number, 50.0, kEps);
    // VRAM 已用/總量文字表述。
    ASSERT_TRUE(v->value().text.has_value());
    EXPECT_EQ(*v->value().text, "4.0 GiB / 8.0 GiB");

    const auto* t = reg.get("gpu.temp")->find_instance("gpu0");
    ASSERT_NE(t, nullptr);
    ASSERT_TRUE(t->value().valid);
    EXPECT_NEAR(t->value().number, 65.0, kEps);
}

// ===========================================================================
// 多 GPU：每張一組實例
// ===========================================================================
TEST(GpuStatsProvider, MultiGpuEnumeratesPerGpuInstances) {
    auto src = makeSource({
        GpuStat::of(0.10, 1 * kGiB, 8 * kGiB, 50.0),
        GpuStat::of(0.90, 7 * kGiB, 8 * kGiB, 80.0),
    });
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.gpu_count(), 2u);

    auto usage = reg.get("gpu.usage");
    ASSERT_EQ(usage->instance_count(), 2u);
    EXPECT_EQ(usage->instance(1).instance_id(), "gpu1");
    EXPECT_EQ(usage->instance(1).label(), "GPU 1");
    EXPECT_NEAR(usage->find_instance("gpu0")->value().number, 10.0, kEps);
    EXPECT_NEAR(usage->find_instance("gpu1")->value().number, 90.0, kEps);

    auto temp = reg.get("gpu.temp");
    EXPECT_NEAR(temp->find_instance("gpu0")->value().number, 50.0, kEps);
    EXPECT_NEAR(temp->find_instance("gpu1")->value().number, 80.0, kEps);
}

// ===========================================================================
// 0% / 100% 邊界
// ===========================================================================
TEST(GpuStatsProvider, ZeroAndHundredPercentBoundaries) {
    auto src = makeSource({
        GpuStat::of(0.0, 0, 8 * kGiB, 40.0),          // 使用率 0%、VRAM 0%
        GpuStat::of(1.0, 8 * kGiB, 8 * kGiB, 90.0),   // 使用率 100%、VRAM 100%
    });
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    auto usage = reg.get("gpu.usage");
    auto vram = reg.get("gpu.vram");
    EXPECT_NEAR(usage->find_instance("gpu0")->value().number, 0.0, kEps);
    EXPECT_NEAR(usage->find_instance("gpu1")->value().number, 100.0, kEps);
    EXPECT_NEAR(vram->find_instance("gpu0")->value().number, 0.0, kEps);
    EXPECT_NEAR(vram->find_instance("gpu1")->value().number, 100.0, kEps);
}

// vram_ratio 自由函式邊界。
TEST(VramRatio, Boundaries) {
    EXPECT_NEAR(vram_ratio(0, 8 * kGiB), 0.0, kEps);         // 0%
    EXPECT_NEAR(vram_ratio(4 * kGiB, 8 * kGiB), 0.5, kEps);  // 50%
    EXPECT_NEAR(vram_ratio(8 * kGiB, 8 * kGiB), 1.0, kEps);  // 100%
    EXPECT_NEAR(vram_ratio(100, 0), 0.0, kEps);              // 總量 0 → 0（不謊報）
    EXPECT_NEAR(vram_ratio(9, 8), 1.0, kEps);                // used>total → 夾到 1.0
}

// ===========================================================================
// GPU 數變動：增 / 減
// ===========================================================================
TEST(GpuStatsProvider, GpuCountIncreaseAddsInstances) {
    auto src = makeSource({GpuStat::of(0.5, 4 * kGiB, 8 * kGiB, 60.0)});  // 1 GPU
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.gpu_count(), 1u);
    auto usage = reg.get("gpu.usage");
    const auto* g0_before = usage->find_instance("gpu0");

    // 增為 2 GPU。
    src->set_gpus({
        GpuStat::of(0.5, 4 * kGiB, 8 * kGiB, 60.0),
        GpuStat::of(0.3, 2 * kGiB, 8 * kGiB, 55.0),
    });
    p.sample();
    EXPECT_EQ(p.gpu_count(), 2u);
    EXPECT_EQ(usage->instance_count(), 2u);
    // 既有 gpu0 參照仍有效（unique_ptr 持有實例）。
    EXPECT_EQ(usage->find_instance("gpu0"), g0_before);
    EXPECT_NE(usage->find_instance("gpu1"), nullptr);
    EXPECT_NEAR(usage->find_instance("gpu1")->value().number, 30.0, kEps);
}

TEST(GpuStatsProvider, GpuCountDecreaseMarksMissingUnknown) {
    auto src = makeSource({
        GpuStat::of(0.5, 4 * kGiB, 8 * kGiB, 60.0),
        GpuStat::of(0.3, 2 * kGiB, 8 * kGiB, 55.0),
    });  // 2 GPU
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.gpu_count(), 2u);

    // 降為 1 GPU。
    src->set_gpus({GpuStat::of(0.5, 4 * kGiB, 8 * kGiB, 60.0)});
    p.sample();
    // 實例數不縮減（2 張實例仍在）。
    EXPECT_EQ(p.gpu_count(), 2u);
    auto usage = reg.get("gpu.usage");
    auto vram = reg.get("gpu.vram");
    auto temp = reg.get("gpu.temp");
    EXPECT_TRUE(usage->find_instance("gpu0")->value().valid);
    // gpu1 下線 → 三個指標皆未知。
    EXPECT_FALSE(usage->find_instance("gpu1")->value().valid);
    EXPECT_FALSE(vram->find_instance("gpu1")->value().valid);
    EXPECT_FALSE(temp->find_instance("gpu1")->value().valid);
}

// ===========================================================================
// 無讀值誠實 invalid（不謊報 0）
// ===========================================================================
TEST(GpuStatsProvider, PerFieldInvalidIsHonest) {
    // 一張 GPU：使用率有讀、VRAM 無讀、溫度無讀。
    GpuStat g;
    g.usage = 0.4;      g.usage_valid = true;
    g.vram_valid = false;
    g.temp_valid = false;
    auto src = makeSource({g});
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    EXPECT_TRUE(reg.get("gpu.usage")->find_instance("gpu0")->value().valid);
    EXPECT_NEAR(reg.get("gpu.usage")->find_instance("gpu0")->value().number, 40.0, kEps);
    // VRAM / 溫度無讀 → 未知，而非 0。
    EXPECT_FALSE(reg.get("gpu.vram")->find_instance("gpu0")->value().valid);
    EXPECT_FALSE(reg.get("gpu.temp")->find_instance("gpu0")->value().valid);
}

TEST(GpuStatsProvider, VramTotalZeroTreatedUnknown) {
    // vram_valid 為真但總量 0 → 百分比無從計算 → 視為未知（不謊報 0）。
    GpuStat g = GpuStat::of(0.5, 0, 0, 60.0);
    auto src = makeSource({g});
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_FALSE(reg.get("gpu.vram")->find_instance("gpu0")->value().valid);
    // 使用率 / 溫度不受影響。
    EXPECT_TRUE(reg.get("gpu.usage")->find_instance("gpu0")->value().valid);
    EXPECT_TRUE(reg.get("gpu.temp")->find_instance("gpu0")->value().valid);
}

// 無讀值不推入歷史（不污染序列）。
TEST(GpuStatsProvider, UnknownNotPushedToHistory) {
    GpuStat g;
    g.usage_valid = false;
    g.vram_valid = false;
    g.temp_valid = false;
    auto src = makeSource({g});
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    p.sample();
    p.sample();
    // 三次未知（register + 2×sample）→ 歷史仍空。
    EXPECT_TRUE(reg.get("gpu.usage")->find_instance("gpu0")->history().empty());
    EXPECT_TRUE(reg.get("gpu.temp")->find_instance("gpu0")->history().empty());
}

// ===========================================================================
// 經 E2-02 頻率採樣（除頻排程）
// ===========================================================================
TEST(GpuStatsProvider, SampledViaE2_02Scheduler) {
    auto src = makeSource({GpuStat::of(0.5, 4 * kGiB, 8 * kGiB, 60.0)});
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // register 內首次 sample → 歷史 1 筆

    SamplingScheduler sched;
    sched.add_demand(GpuStatsProvider::kUsageId, p.sampling_tier());  // Normal 間隔 8

    // 推進 24 個 tick，Normal 間隔 8 → 於 tick 8/16/24 到期採樣 3 次。
    int sampled = 0;
    for (ds::metrics::Tick t = 1; t <= 24; ++t) {
        auto due = sched.advance(t);
        for (const auto& id : due) {
            if (id == GpuStatsProvider::kUsageId) {
                p.sample();
                ++sampled;
            }
        }
    }
    EXPECT_EQ(sampled, 3);

    const auto& hist = reg.get("gpu.usage")->find_instance("gpu0")->history();
    // register 首採 1 筆 + 排程 3 筆 = 4 筆，各 50%。
    EXPECT_EQ(hist.size(), 4u);
    EXPECT_NEAR(hist.latest(), 50.0, kEps);
}

// 除頻：多消費者同一指標合併，最高頻者供給。
TEST(GpuStatsProvider, DeFrequencyCoalescesDemands) {
    SamplingScheduler sched;
    auto d_low = sched.add_demand(GpuStatsProvider::kUsageId, SamplingTier::Low);
    sched.add_demand(GpuStatsProvider::kUsageId, SamplingTier::High);
    ASSERT_TRUE(sched.effective_tier(GpuStatsProvider::kUsageId).has_value());
    EXPECT_EQ(*sched.effective_tier(GpuStatsProvider::kUsageId), SamplingTier::High);
    EXPECT_EQ(sched.demand_count(GpuStatsProvider::kUsageId), 2u);
    EXPECT_TRUE(sched.remove_demand(d_low));
    EXPECT_EQ(*sched.effective_tier(GpuStatsProvider::kUsageId), SamplingTier::High);
}

// ===========================================================================
// null 來源行為（固定 / 序列 / 空）
// ===========================================================================
TEST(GpuStatsProvider, NullSourceIsConservative) {
    // source 為 null 指標：仍掛上三個指標，各 0 實例、不崩。
    GpuStatsProvider p{nullptr};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 3u);
    EXPECT_EQ(reg.get("gpu.usage")->instance_count(), 0u);
    EXPECT_EQ(reg.get("gpu.vram")->instance_count(), 0u);
    EXPECT_EQ(reg.get("gpu.temp")->instance_count(), 0u);
    EXPECT_EQ(p.gpu_count(), 0u);
    p.sample();  // 不崩
    EXPECT_EQ(p.gpu_count(), 0u);
}

TEST(GpuStatsProvider, NullGpuStatSourceDefaultEmpty) {
    // NullGpuStatSource 預設（未注入）→ 空快照、0 GPU。
    auto src = std::make_shared<NullGpuStatSource>();
    EXPECT_TRUE(src->empty());
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.gpu_count(), 0u);

    // 之後注入資料 → sample() 後可讀。
    src->set_gpus({GpuStat::of(0.6, 6 * kGiB, 8 * kGiB, 70.0)});
    p.sample();
    EXPECT_EQ(p.gpu_count(), 1u);
    EXPECT_NEAR(reg.get("gpu.usage")->find_instance("gpu0")->value().number, 60.0, kEps);
}

// 序列來源：每次 sample() 回下一份；列盡回最後一份。
TEST(NullGpuStatSource, SequenceThenExhaustion) {
    NullGpuStatSource src{std::vector<GpuStatSample>{
        makeSample({GpuStat::of(0.1, 1 * kGiB, 8 * kGiB, 50.0)}),
        makeSample({GpuStat::of(0.2, 2 * kGiB, 8 * kGiB, 55.0)}),
    }};
    EXPECT_EQ(src.size(), 2u);
    EXPECT_NEAR(src.sample().gpus[0].usage, 0.1, kEps);
    EXPECT_NEAR(src.sample().gpus[0].usage, 0.2, kEps);
    // 列盡 → 持續回最後一份。
    EXPECT_NEAR(src.sample().gpus[0].usage, 0.2, kEps);
}

// 固定來源：每次 sample() 皆回同一份。
TEST(NullGpuStatSource, FixedReturnsSame) {
    NullGpuStatSource src{makeSample({GpuStat::of(0.42, 4 * kGiB, 8 * kGiB, 65.0)})};
    EXPECT_NEAR(src.sample().gpus[0].usage, 0.42, kEps);
    EXPECT_NEAR(src.sample().gpus[0].usage, 0.42, kEps);
    src.clear();
    EXPECT_TRUE(src.sample().empty());  // clear → 空快照
}

// 序列驅動提供者：GPU 數與各欄值隨序列前進而變。
TEST(GpuStatsProvider, SequenceDrivesProviderAcrossSamples) {
    auto src = std::make_shared<NullGpuStatSource>(std::vector<GpuStatSample>{
        makeSample({GpuStat::of(0.10, 1 * kGiB, 8 * kGiB, 50.0)}),
        makeSample({GpuStat::of(0.20, 2 * kGiB, 8 * kGiB, 55.0)}),
        makeSample({GpuStat::of(0.30, 3 * kGiB, 8 * kGiB, 60.0)}),
    });
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // 消耗第 0 份 → 10%
    auto usage = reg.get("gpu.usage");
    EXPECT_NEAR(usage->find_instance("gpu0")->value().number, 10.0, kEps);
    p.sample();  // 第 1 份 → 20%
    EXPECT_NEAR(usage->find_instance("gpu0")->value().number, 20.0, kEps);
    p.sample();  // 第 2 份 → 30%
    EXPECT_NEAR(usage->find_instance("gpu0")->value().number, 30.0, kEps);
    // 歷史累積 3 筆（register + 2 sample）。
    EXPECT_EQ(usage->find_instance("gpu0")->history().size(), 3u);
}

// sample() 未 register_metrics 時為 no-op（不崩）。
TEST(GpuStatsProvider, SampleBeforeRegisterIsNoop) {
    GpuStatsProvider p{makeSource({GpuStat::of(0.5, 4 * kGiB, 8 * kGiB, 60.0)})};
    p.sample();  // 尚未 register → no-op，不崩
    EXPECT_EQ(p.gpu_count(), 0u);
}

// ===========================================================================
// 範圍 / 消費者範式 / 重複註冊
// ===========================================================================
TEST(GpuStatsProvider, RangesUsageVramBoundedTempLowerBounded) {
    auto src = makeSource({GpuStat::of(0.5, 4 * kGiB, 8 * kGiB, 60.0)});
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    auto ur = reg.get("gpu.usage")->range();
    ASSERT_TRUE(ur.is_bounded());
    EXPECT_NEAR(*ur.min, 0.0, kEps);
    EXPECT_NEAR(*ur.max, 100.0, kEps);
    auto vr = reg.get("gpu.vram")->range();
    ASSERT_TRUE(vr.is_bounded());
    EXPECT_NEAR(*vr.max, 100.0, kEps);
    // 溫度：下界 0、無上界。
    auto tr = reg.get("gpu.temp")->range();
    EXPECT_TRUE(tr.has_min());
    EXPECT_FALSE(tr.has_max());
    EXPECT_NEAR(*tr.min, 0.0, kEps);
    // 50% 正規化到 0.5（usage）。
    auto norm = ur.normalized(50.0);
    ASSERT_TRUE(norm.has_value());
    EXPECT_NEAR(*norm, 0.5, kEps);
}

// 掛件風格消費者：只透過 E2-01 registry / Metric 抽象介面走訪，完全不觸及具體型別。
TEST(GpuStatsProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = makeSource({
        GpuStat::of(0.20, 2 * kGiB, 8 * kGiB, 50.0),
        GpuStat::of(0.80, 6 * kGiB, 8 * kGiB, 70.0),
    });
    GpuStatsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    std::shared_ptr<Metric> m = reg.get("gpu.usage");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), "%");
    double sum = 0.0;
    for (std::size_t i = 0; i < m->instance_count(); ++i) {
        const auto& inst = m->instance(i);
        if (inst.value().valid) sum += inst.value().number;
    }
    EXPECT_NEAR(sum, 100.0, kEps);  // 20 + 80
}

TEST(GpuStatsProvider, DuplicateRegistrationRejected) {
    GpuStatsProvider p1{makeSource({GpuStat::of(0.5, 4 * kGiB, 8 * kGiB, 60.0)})};
    GpuStatsProvider p2{makeSource({GpuStat::of(0.9, 7 * kGiB, 8 * kGiB, 80.0)})};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 3u);
    // 第二個提供者掛同一組 id → 三個皆保守拒絕（不覆寫既有）。
    EXPECT_EQ(reg.add_provider(p2), 0u);
    EXPECT_EQ(reg.size(), 3u);
}

// ===========================================================================
// 自由函式 / 值模型單元
// ===========================================================================
TEST(HumanizeBytes, Units) {
    EXPECT_EQ(humanize_bytes(0), "0 B");
    EXPECT_EQ(humanize_bytes(512), "512 B");
    EXPECT_EQ(humanize_bytes(1024), "1.0 KiB");
    EXPECT_EQ(humanize_bytes(1024ULL * 1024ULL), "1.0 MiB");
    EXPECT_EQ(humanize_bytes(kGiB), "1.0 GiB");
    EXPECT_EQ(humanize_bytes(8 * kGiB), "8.0 GiB");
    // 4.5 GiB → "4.5 GiB"（一位小數）。
    EXPECT_EQ(humanize_bytes(kGiB + kGiB / 2), "1.5 GiB");
}

TEST(GpuStat, UnknownAndOfFactories) {
    GpuStat u = GpuStat::unknown();
    EXPECT_FALSE(u.usage_valid);
    EXPECT_FALSE(u.vram_valid);
    EXPECT_FALSE(u.temp_valid);

    GpuStat g = GpuStat::of(0.5, 4 * kGiB, 8 * kGiB, 65.0);
    EXPECT_TRUE(g.usage_valid);
    EXPECT_TRUE(g.vram_valid);
    EXPECT_TRUE(g.temp_valid);
    EXPECT_NEAR(g.usage, 0.5, kEps);
    EXPECT_EQ(g.vram_used, 4 * kGiB);
    EXPECT_NEAR(g.temperature, 65.0, kEps);
    EXPECT_NE(g, u);
}

TEST(GpuStatSample, EqualityAndCount) {
    GpuStatSample a = makeSample({GpuStat::of(0.5, 4 * kGiB, 8 * kGiB, 60.0)});
    GpuStatSample b = a;
    EXPECT_EQ(a, b);
    EXPECT_EQ(a.gpu_count(), 1u);
    b.gpus.push_back(GpuStat::unknown());
    EXPECT_NE(a, b);
    EXPECT_EQ(b.gpu_count(), 2u);
    EXPECT_FALSE(b.empty());
    EXPECT_TRUE(GpuStatSample{}.empty());
}
