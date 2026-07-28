// E2-06 儲存容量 — 測試（gtest）
//
// 覆蓋：提供者身分、註冊四個平行指標到 E2-01 registry、每磁碟容量欄位（total/used/free
// bytes）、使用率 % 計算、多磁碟列舉、磁碟數變動（增 → 動態新增實例、減 → 多出者設未知）、
// 0% / 滿載 / total=0 邊界、無讀值 invalid（磁碟 valid=false / null 來源 / source=nullptr）、
// 經 E2-02 頻率採樣（除頻排程）、null 來源行為、範圍（usage bounded[0,100]、bytes at_least(0)）、
// 消費者只走 E2-01 抽象介面、重複註冊保守拒絕、使用率自由函式邊界。
// 相位 1：只驗介面 + 注入式來源行為，不含任何平台分支。
#include "storage_capacity.hpp"

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
using ds::sysinfo::DiskCapacity;
using ds::sysinfo::disk_usage_ratio;
using ds::sysinfo::NullStorageStatSource;
using ds::sysinfo::StorageCapacityProvider;
using ds::sysinfo::StorageStatSource;

namespace {

constexpr double kEps = 1e-6;
constexpr std::uint64_t kGiB = 1024ull * 1024ull * 1024ull;

// 便利：以 GiB 表達的一顆磁碟（total/used/free）。
DiskCapacity disk(std::string id, std::string name, std::uint64_t total_gib,
                  std::uint64_t used_gib, std::uint64_t free_gib) {
    return DiskCapacity::of(std::move(id), std::move(name), total_gib * kGiB,
                            used_gib * kGiB, free_gib * kGiB);
}

std::shared_ptr<NullStorageStatSource> makeSource(std::vector<DiskCapacity> disks) {
    return std::make_shared<NullStorageStatSource>(std::move(disks));
}

}  // namespace

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(StorageCapacityProvider, ProviderIdAndMetricIdsStable) {
    StorageCapacityProvider p{std::make_shared<NullStorageStatSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.storage");
    EXPECT_EQ(std::string(StorageCapacityProvider::kMetricUsage), "storage.usage");
    EXPECT_EQ(std::string(StorageCapacityProvider::kMetricTotal), "storage.capacity.total");
    EXPECT_EQ(std::string(StorageCapacityProvider::kMetricUsed), "storage.capacity.used");
    EXPECT_EQ(std::string(StorageCapacityProvider::kMetricFree), "storage.capacity.free");
    EXPECT_EQ(std::string(StorageCapacityProvider::kUnitPercent), "%");
    EXPECT_EQ(std::string(StorageCapacityProvider::kUnitBytes), "B");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(StorageCapacityProvider, IsMetricProvider) {
    StorageCapacityProvider p{std::make_shared<NullStorageStatSource>()};
    MetricProvider& mp = p;  // 可上轉
    EXPECT_EQ(mp.provider_id(), "sysinfo.storage");
}

// 預設採集分級 = Low（容量變動慢），可由建構子覆寫。
TEST(StorageCapacityProvider, SamplingTierDefaultsLowOverridable) {
    StorageCapacityProvider def{std::make_shared<NullStorageStatSource>()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::Low);

    StorageCapacityProvider hi{std::make_shared<NullStorageStatSource>(),
                               StorageCapacityProvider::kDefaultHistory, SamplingTier::High};
    EXPECT_EQ(hi.sampling_tier(), SamplingTier::High);
}

// ===========================================================================
// 註冊四個平行指標
// ===========================================================================
TEST(StorageCapacityProvider, RegistersFourParallelMetrics) {
    auto src = makeSource({disk("/", "Macintosh HD", 500, 250, 250)});
    StorageCapacityProvider p{src};
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);

    EXPECT_EQ(added, 4u);  // usage / total / used / free
    EXPECT_TRUE(reg.contains("storage.usage"));
    EXPECT_TRUE(reg.contains("storage.capacity.total"));
    EXPECT_TRUE(reg.contains("storage.capacity.used"));
    EXPECT_TRUE(reg.contains("storage.capacity.free"));
    EXPECT_EQ(p.disk_count(), 1u);
}

// ===========================================================================
// 每磁碟容量欄位 + 使用率
// ===========================================================================
TEST(StorageCapacityProvider, PerDiskCapacityFieldsAndUsage) {
    // 500 GiB 總、250 已用、250 可用 → 使用率 50%。
    auto src = makeSource({disk("/", "Macintosh HD", 500, 250, 250)});
    StorageCapacityProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    auto usage = reg.get("storage.usage");
    auto total = reg.get("storage.capacity.total");
    auto used = reg.get("storage.capacity.used");
    auto free = reg.get("storage.capacity.free");
    ASSERT_NE(usage, nullptr);
    ASSERT_NE(total, nullptr);
    ASSERT_NE(used, nullptr);
    ASSERT_NE(free, nullptr);

    // 四指標共用同一磁碟實例 id / label。
    const auto* u = usage->find_instance("/");
    const auto* t = total->find_instance("/");
    const auto* us = used->find_instance("/");
    const auto* f = free->find_instance("/");
    ASSERT_NE(u, nullptr);
    ASSERT_NE(t, nullptr);
    ASSERT_NE(us, nullptr);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(u->label(), "Macintosh HD");

    ASSERT_TRUE(u->value().valid);
    EXPECT_NEAR(u->value().number, 50.0, kEps);                         // 250/500 = 50%
    EXPECT_NEAR(t->value().number, 500.0 * kGiB, 1.0);                  // total bytes
    EXPECT_NEAR(us->value().number, 250.0 * kGiB, 1.0);                 // used bytes
    EXPECT_NEAR(f->value().number, 250.0 * kGiB, 1.0);                  // free bytes
}

// ===========================================================================
// 多磁碟
// ===========================================================================
TEST(StorageCapacityProvider, MultipleDisksEnumerated) {
    auto src = makeSource({
        disk("/", "Macintosh HD", 500, 100, 400),      // 20%
        disk("/Volumes/Data", "Data", 1000, 750, 250), // 75%
        disk("/Volumes/USB", "USB", 64, 32, 32),       // 50%
    });
    StorageCapacityProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.disk_count(), 3u);

    auto usage = reg.get("storage.usage");
    ASSERT_NE(usage, nullptr);
    EXPECT_EQ(usage->instance_count(), 3u);
    // 列舉順序 = 來源順序（決定性）。
    EXPECT_EQ(usage->instance(0).instance_id(), "/");
    EXPECT_EQ(usage->instance(1).instance_id(), "/Volumes/Data");
    EXPECT_EQ(usage->instance(2).instance_id(), "/Volumes/USB");
    EXPECT_NEAR(usage->find_instance("/")->value().number, 20.0, kEps);
    EXPECT_NEAR(usage->find_instance("/Volumes/Data")->value().number, 75.0, kEps);
    EXPECT_NEAR(usage->find_instance("/Volumes/USB")->value().number, 50.0, kEps);
}

// ===========================================================================
// 磁碟數變動
// ===========================================================================
TEST(StorageCapacityProvider, DiskCountGrowsAddsInstances) {
    auto src = makeSource({disk("/", "HD", 500, 250, 250)});
    StorageCapacityProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.disk_count(), 1u);

    auto usage = reg.get("storage.usage");
    const auto* d0_before = usage->find_instance("/");
    ASSERT_NE(d0_before, nullptr);

    // 掛載第二顆磁碟。
    src->add_disk(disk("/Volumes/Ext", "Ext", 200, 50, 150));  // 25%
    p.sample();
    EXPECT_EQ(p.disk_count(), 2u);
    EXPECT_EQ(usage->instance_count(), 2u);
    // 既有磁碟參照仍有效（unique_ptr 持有實例）。
    EXPECT_EQ(usage->find_instance("/"), d0_before);
    EXPECT_NEAR(usage->find_instance("/Volumes/Ext")->value().number, 25.0, kEps);
}

TEST(StorageCapacityProvider, DiskCountDecreaseMarksMissingUnknown) {
    auto src = makeSource({
        disk("/", "HD", 500, 250, 250),
        disk("/Volumes/Ext", "Ext", 200, 100, 100),
    });
    StorageCapacityProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.disk_count(), 2u);

    // 卸載第二顆（來源只剩第一顆）。
    src->set_disks({disk("/", "HD", 500, 300, 200)});  // 使用率變 60%
    p.sample();
    auto usage = reg.get("storage.usage");
    auto total = reg.get("storage.capacity.total");
    // 實例數不縮減（誠實表達卸載）。
    EXPECT_EQ(p.disk_count(), 2u);
    EXPECT_TRUE(usage->find_instance("/")->value().valid);
    EXPECT_NEAR(usage->find_instance("/")->value().number, 60.0, kEps);
    // 卸載的磁碟 → 四槽皆未知。
    EXPECT_FALSE(usage->find_instance("/Volumes/Ext")->value().valid);
    EXPECT_FALSE(total->find_instance("/Volumes/Ext")->value().valid);
}

// ===========================================================================
// 0% / 滿載 / total=0 邊界
// ===========================================================================
TEST(DiskUsageRatio, BoundaryCases) {
    // 0%：used=0。
    EXPECT_NEAR(disk_usage_ratio(0, 1000), 0.0, kEps);
    // 100%：used==total。
    EXPECT_NEAR(disk_usage_ratio(1000, 1000), 1.0, kEps);
    // total=0（無容量）→ 0（不謊報）。
    EXPECT_NEAR(disk_usage_ratio(0, 0), 0.0, kEps);
    EXPECT_NEAR(disk_usage_ratio(500, 0), 0.0, kEps);
    // used>total（理論不該發生）→ 夾到滿載。
    EXPECT_NEAR(disk_usage_ratio(1500, 1000), 1.0, kEps);
}

TEST(StorageCapacityProvider, ZeroAndFullThroughProvider) {
    auto src = makeSource({
        DiskCapacity::of("/empty", "Empty", 1000 * kGiB, 0, 1000 * kGiB),   // 0%
        DiskCapacity::of("/full", "Full", 1000 * kGiB, 1000 * kGiB, 0),     // 100%
        DiskCapacity::of("/zero", "Zero", 0, 0, 0),                         // total=0 → 0%
    });
    StorageCapacityProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto usage = reg.get("storage.usage");
    EXPECT_NEAR(usage->find_instance("/empty")->value().number, 0.0, kEps);
    EXPECT_NEAR(usage->find_instance("/full")->value().number, 100.0, kEps);
    EXPECT_NEAR(usage->find_instance("/zero")->value().number, 0.0, kEps);
    // total=0 磁碟仍為有效讀值（valid），只是使用率 0。
    EXPECT_TRUE(usage->find_instance("/zero")->value().valid);
    EXPECT_NEAR(reg.get("storage.capacity.total")->find_instance("/zero")->value().number,
                0.0, kEps);
}

// DiskCapacity::usage_ratio 成員便利函式。
TEST(DiskCapacity, UsageRatioMember) {
    DiskCapacity d = DiskCapacity::of("/", "HD", 400, 100, 300);
    EXPECT_NEAR(d.usage_ratio(), 0.25, kEps);
}

// ===========================================================================
// 無讀值 invalid
// ===========================================================================
TEST(StorageCapacityProvider, InvalidDiskMarkedUnknown) {
    // 磁碟存在但無法查詢（valid=false）→ 四槽皆未知。
    auto src = makeSource({DiskCapacity::unknown("/", "HD")});
    StorageCapacityProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.disk_count(), 1u);

    EXPECT_FALSE(reg.get("storage.usage")->find_instance("/")->value().valid);
    EXPECT_FALSE(reg.get("storage.capacity.total")->find_instance("/")->value().valid);
    EXPECT_FALSE(reg.get("storage.capacity.used")->find_instance("/")->value().valid);
    EXPECT_FALSE(reg.get("storage.capacity.free")->find_instance("/")->value().valid);
}

TEST(StorageCapacityProvider, NullSourcePointerConservative) {
    // source 為 null 指標：仍掛上四指標、無磁碟、不崩。
    StorageCapacityProvider p{nullptr};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 4u);
    EXPECT_EQ(p.disk_count(), 0u);
    EXPECT_EQ(reg.get("storage.usage")->instance_count(), 0u);
    // sample() 不崩。
    p.sample();
    EXPECT_EQ(p.disk_count(), 0u);
}

TEST(StorageCapacityProvider, NullStorageSourceDefaultEmpty) {
    // NullStorageStatSource 預設（未注入）→ 無磁碟；之後注入 → sample() 後可讀。
    auto src = std::make_shared<NullStorageStatSource>();
    StorageCapacityProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.disk_count(), 0u);

    src->add_disk(disk("/", "HD", 800, 200, 600));  // 25%
    p.sample();
    EXPECT_EQ(p.disk_count(), 1u);
    EXPECT_NEAR(reg.get("storage.usage")->find_instance("/")->value().number, 25.0, kEps);
}

// sample() 未 register_metrics 時為 no-op（不崩）。
TEST(StorageCapacityProvider, SampleBeforeRegisterIsNoop) {
    StorageCapacityProvider p{makeSource({disk("/", "HD", 100, 50, 50)})};
    p.sample();  // 尚未 register → no-op，不崩
    EXPECT_EQ(p.disk_count(), 0u);
}

// ===========================================================================
// 經 E2-02 頻率採樣（除頻排程）
// ===========================================================================
TEST(StorageCapacityProvider, SampledViaE2_02Scheduler) {
    auto src = makeSource({disk("/", "HD", 1000, 100, 900)});  // 10%
    StorageCapacityProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // register 時首採 → 歷史已有一筆

    SamplingScheduler sched;  // 預設 Low 間隔=64
    sched.add_demand(StorageCapacityProvider::kMetricUsage, p.sampling_tier());
    ASSERT_TRUE(sched.effective_tier(StorageCapacityProvider::kMetricUsage).has_value());
    EXPECT_EQ(*sched.effective_tier(StorageCapacityProvider::kMetricUsage), SamplingTier::Low);

    // 每次到期把 used 加 100 GiB（模擬磁碟漸滿），推進 3 個 Low 週期。
    int sampled = 0;
    std::uint64_t used = 100;
    for (ds::metrics::Tick t = 64; t <= 64 * 3; t += 64) {
        auto due = sched.advance(t);
        for (const auto& id : due) {
            if (id == StorageCapacityProvider::kMetricUsage) {
                used += 100;
                src->set_disks({disk("/", "HD", 1000, used, 1000 - used)});
                p.sample();
                ++sampled;
            }
        }
    }
    EXPECT_EQ(sampled, 3);

    const auto& hist = reg.get("storage.usage")->find_instance("/")->history();
    // register 首採 1 筆 + 3 次採樣 = 4 筆。
    EXPECT_EQ(hist.size(), 4u);
    EXPECT_NEAR(hist.latest(), 40.0, kEps);  // 最後 used=400/1000 = 40%
}

// 除頻：多消費者同一指標合併，最高頻者供給。
TEST(StorageCapacityProvider, DeFrequencyCoalescesDemands) {
    StorageCapacityProvider p{makeSource({disk("/", "HD", 100, 50, 50)})};
    SamplingScheduler sched;
    auto d_low = sched.add_demand(StorageCapacityProvider::kMetricUsage, SamplingTier::Low);
    sched.add_demand(StorageCapacityProvider::kMetricUsage, SamplingTier::High);
    ASSERT_TRUE(sched.effective_tier(StorageCapacityProvider::kMetricUsage).has_value());
    EXPECT_EQ(*sched.effective_tier(StorageCapacityProvider::kMetricUsage), SamplingTier::High);
    EXPECT_EQ(sched.demand_count(StorageCapacityProvider::kMetricUsage), 2u);

    EXPECT_TRUE(sched.remove_demand(d_low));
    EXPECT_TRUE(sched.tracks(StorageCapacityProvider::kMetricUsage));
    EXPECT_EQ(*sched.effective_tier(StorageCapacityProvider::kMetricUsage), SamplingTier::High);
}

// ===========================================================================
// 範圍 / 消費者範式 / 重複註冊
// ===========================================================================
TEST(StorageCapacityProvider, RangesUsageBoundedBytesAtLeastZero) {
    StorageCapacityProvider p{makeSource({disk("/", "HD", 500, 250, 250)})};
    MetricRegistry reg;
    reg.add_provider(p);

    auto usage = reg.get("storage.usage");
    auto ur = usage->range();
    ASSERT_TRUE(ur.is_bounded());
    EXPECT_NEAR(*ur.min, 0.0, kEps);
    EXPECT_NEAR(*ur.max, 100.0, kEps);
    auto norm = ur.normalized(50.0);
    ASSERT_TRUE(norm.has_value());
    EXPECT_NEAR(*norm, 0.5, kEps);  // 50% → 0.5

    // 容量指標：下界 0、上無界（非有界，不可正規化）。
    auto tr = reg.get("storage.capacity.total")->range();
    EXPECT_TRUE(tr.has_min());
    EXPECT_FALSE(tr.has_max());
    EXPECT_FALSE(tr.is_bounded());
}

// 掛件風格消費者：只透過 E2-01 registry / Metric 抽象介面走訪，完全不觸及具體型別。
TEST(StorageCapacityProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = makeSource({
        disk("/", "HD", 500, 250, 250),
        disk("/Volumes/Data", "Data", 500, 250, 250),
    });
    StorageCapacityProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    std::shared_ptr<Metric> m = reg.get("storage.usage");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), "%");
    // 兩顆各 50% → 平均 50%。
    double sum = 0.0;
    for (std::size_t i = 0; i < m->instance_count(); ++i) {
        const auto& inst = m->instance(i);
        if (inst.value().valid) sum += inst.value().number;
    }
    EXPECT_NEAR(sum / static_cast<double>(m->instance_count()), 50.0, kEps);
}

TEST(StorageCapacityProvider, DuplicateRegistrationRejected) {
    StorageCapacityProvider p1{makeSource({disk("/", "HD", 100, 50, 50)})};
    StorageCapacityProvider p2{makeSource({disk("/", "HD2", 200, 100, 100)})};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 4u);
    // 第二個提供者掛同一組 id → 四個皆保守拒絕（不覆寫既有）。
    EXPECT_EQ(reg.add_provider(p2), 0u);
    EXPECT_EQ(reg.size(), 4u);
}

// ===========================================================================
// 值模型單元
// ===========================================================================
TEST(DiskCapacity, FactoriesAndEquality) {
    DiskCapacity a = DiskCapacity::of("/", "HD", 100, 40, 60);
    EXPECT_TRUE(a.valid);
    EXPECT_EQ(a.total_bytes, 100u);
    EXPECT_EQ(a.used_bytes, 40u);
    EXPECT_EQ(a.free_bytes, 60u);

    DiskCapacity u = DiskCapacity::unknown("/", "HD");
    EXPECT_FALSE(u.valid);
    EXPECT_NE(a, u);

    DiskCapacity a2 = DiskCapacity::of("/", "HD", 100, 40, 60);
    EXPECT_EQ(a, a2);
}

TEST(NullStorageStatSource, InjectAndClear) {
    NullStorageStatSource src;
    EXPECT_TRUE(src.empty());
    EXPECT_EQ(src.enumerate().size(), 0u);

    src.add_disk(disk("/", "HD", 100, 50, 50));
    src.add_disk(disk("/b", "B", 200, 100, 100));
    EXPECT_EQ(src.size(), 2u);
    EXPECT_EQ(src.enumerate().size(), 2u);

    src.clear();
    EXPECT_TRUE(src.empty());
}
