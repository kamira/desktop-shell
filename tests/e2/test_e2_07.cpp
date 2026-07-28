// E2-07 儲存 IO 吞吐與佇列 — 測試（gtest）
//
// 覆蓋：提供者身分、註冊五指標到 E2-01 registry、每磁碟實例列舉、讀/寫吞吐差分計算
// （注入兩份累積 bytes + 時間戳 → 算 bytes/sec）、讀/寫 IOPS 差分、佇列深度（瞬時值直接
// 帶出）、多磁碟、首次差分回 invalid（需兩份）、時間差為零 / 為負處理、計數器重置、
// 無讀值誠實 invalid、經 E2-02 頻率採樣（除頻排程）、null 來源、直接速率來源、範圍
// at_least(0)、消費者只走 E2-01 抽象介面、重複註冊保守拒絕、值模型單元。相位 1：只驗
// 介面 + 注入式來源行為，不含任何平台分支。
#include "disk_io.hpp"

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
using ds::sysinfo::counter_rate;
using ds::sysinfo::DifferencingIoRateSource;
using ds::sysinfo::DiskIoCounters;
using ds::sysinfo::DiskIoProvider;
using ds::sysinfo::DiskIoRates;
using ds::sysinfo::DiskIoReading;
using ds::sysinfo::DiskIoSample;
using ds::sysinfo::DiskIoSnapshot;
using ds::sysinfo::DiskIoUsageSample;
using ds::sysinfo::IoRateSource;
using ds::sysinfo::NullIoRateSource;
using ds::sysinfo::NullIoStatSource;
using ds::sysinfo::rates_from_delta;
using ds::sysinfo::usage_from_delta;

namespace {

constexpr double kEps = 1e-9;

// 建一份磁碟快照：一顆磁碟 {id,label} + 累積計數。
DiskIoSnapshot makeSnap(std::string id, std::string label, std::uint64_t rb,
                        std::uint64_t wb, std::uint64_t ro, std::uint64_t wo,
                        double q) {
    return DiskIoSnapshot{std::move(id), std::move(label),
                          DiskIoCounters{rb, wb, ro, wo, q}};
}

// 建一份全磁碟快照（時間戳 + 磁碟列）。
DiskIoSample makeSample(double ts, std::vector<DiskIoSnapshot> disks) {
    DiskIoSample s;
    s.timestamp = ts;
    s.disks = std::move(disks);
    return s;
}

// 直接速率來源：注入一份有效速率快照（單顆磁碟）。
std::shared_ptr<NullIoRateSource> makeRateSource(std::vector<DiskIoReading> disks) {
    DiskIoUsageSample u;
    u.valid = !disks.empty();
    u.disks = std::move(disks);
    auto src = std::make_shared<NullIoRateSource>();
    src->set_usage(std::move(u));
    return src;
}

}  // namespace

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(DiskIoProvider, ProviderIdIsStable) {
    DiskIoProvider p{std::make_shared<NullIoRateSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.disk_io");
}

TEST(DiskIoProvider, DefaultSamplingTierIsNormal) {
    DiskIoProvider p{std::make_shared<NullIoRateSource>()};
    EXPECT_EQ(p.sampling_tier(), SamplingTier::Normal);
}

TEST(DiskIoProvider, SamplingTierOverridable) {
    DiskIoProvider p{std::make_shared<NullIoRateSource>(), 32, SamplingTier::High};
    EXPECT_EQ(p.sampling_tier(), SamplingTier::High);
}

// ===========================================================================
// counter_rate 自由函式（差分核心）
// ===========================================================================
TEST(CounterRate, BasicDelta) {
    // 1000 bytes 在 2 秒內 → 500 bytes/sec。
    EXPECT_NEAR(counter_rate(0, 1000, 2.0), 500.0, kEps);
    EXPECT_NEAR(counter_rate(1000, 3000, 4.0), 500.0, kEps);
}

TEST(CounterRate, ZeroTimeDeltaIsZero) {
    EXPECT_NEAR(counter_rate(0, 1000, 0.0), 0.0, kEps);
}

TEST(CounterRate, NegativeTimeDeltaIsZero) {
    EXPECT_NEAR(counter_rate(0, 1000, -1.0), 0.0, kEps);
}

TEST(CounterRate, CounterResetIsZero) {
    // curr < prev（回繞 / 重置）→ 保守回 0。
    EXPECT_NEAR(counter_rate(5000, 1000, 2.0), 0.0, kEps);
}

TEST(CounterRate, NoDeltaIsZero) {
    EXPECT_NEAR(counter_rate(1000, 1000, 2.0), 0.0, kEps);
}

// ===========================================================================
// rates_from_delta：讀/寫吞吐 + IOPS + 佇列深度（瞬時）
// ===========================================================================
TEST(RatesFromDelta, ThroughputAndIops) {
    DiskIoCounters prev{1000, 2000, 10, 20, 3.0};
    DiskIoCounters curr{5000, 4000, 30, 60, 7.0};
    // dt = 2s：readΔ=4000/2=2000 B/s；writeΔ=2000/2=1000 B/s；
    //          read_opsΔ=20/2=10 IOPS；write_opsΔ=40/2=20 IOPS；queue=瞬時 7。
    DiskIoRates r = rates_from_delta(prev, curr, 2.0);
    EXPECT_NEAR(r.read_bps, 2000.0, kEps);
    EXPECT_NEAR(r.write_bps, 1000.0, kEps);
    EXPECT_NEAR(r.read_iops, 10.0, kEps);
    EXPECT_NEAR(r.write_iops, 20.0, kEps);
    EXPECT_NEAR(r.queue_depth, 7.0, kEps);  // 瞬時值，取當前快照
}

TEST(RatesFromDelta, QueueDepthIsInstantaneousNotDifferenced) {
    // 佇列深度不差分：即使 prev 有值，結果只取 curr 的當前深度。
    DiskIoCounters prev{0, 0, 0, 0, 99.0};
    DiskIoCounters curr{0, 0, 0, 0, 4.0};
    DiskIoRates r = rates_from_delta(prev, curr, 2.0);
    EXPECT_NEAR(r.queue_depth, 4.0, kEps);
}

TEST(RatesFromDelta, ZeroTimeAllRatesZeroButQueueKept) {
    DiskIoCounters prev{1000, 2000, 10, 20, 3.0};
    DiskIoCounters curr{5000, 4000, 30, 60, 7.0};
    DiskIoRates r = rates_from_delta(prev, curr, 0.0);
    EXPECT_NEAR(r.read_bps, 0.0, kEps);
    EXPECT_NEAR(r.write_bps, 0.0, kEps);
    EXPECT_NEAR(r.read_iops, 0.0, kEps);
    EXPECT_NEAR(r.write_iops, 0.0, kEps);
    EXPECT_NEAR(r.queue_depth, 7.0, kEps);  // 瞬時值仍有意義
}

// ===========================================================================
// usage_from_delta：全磁碟差分 + valid 語意
// ===========================================================================
TEST(UsageFromDelta, SingleDiskDifferencing) {
    DiskIoSample prev = makeSample(10.0, {makeSnap("disk0", "Disk 0", 0, 0, 0, 0, 2.0)});
    DiskIoSample curr = makeSample(12.0, {makeSnap("disk0", "Disk 0", 4000, 2000, 20, 40, 5.0)});
    DiskIoUsageSample u = usage_from_delta(prev, curr);
    ASSERT_TRUE(u.valid);
    ASSERT_EQ(u.disk_count(), 1u);
    EXPECT_EQ(u.disks[0].id, "disk0");
    EXPECT_EQ(u.disks[0].label, "Disk 0");
    EXPECT_NEAR(u.disks[0].rates.read_bps, 2000.0, kEps);   // 4000/2
    EXPECT_NEAR(u.disks[0].rates.write_bps, 1000.0, kEps);  // 2000/2
    EXPECT_NEAR(u.disks[0].rates.read_iops, 10.0, kEps);    // 20/2
    EXPECT_NEAR(u.disks[0].rates.write_iops, 20.0, kEps);   // 40/2
    EXPECT_NEAR(u.disks[0].rates.queue_depth, 5.0, kEps);
}

TEST(UsageFromDelta, MultiDisk) {
    DiskIoSample prev = makeSample(0.0, {
        makeSnap("disk0", "Disk 0", 0, 0, 0, 0, 1.0),
        makeSnap("disk1", "Disk 1", 100, 0, 0, 0, 2.0),
    });
    DiskIoSample curr = makeSample(1.0, {
        makeSnap("disk0", "Disk 0", 1000, 0, 0, 0, 3.0),
        makeSnap("disk1", "Disk 1", 100, 500, 0, 0, 4.0),
    });
    DiskIoUsageSample u = usage_from_delta(prev, curr);
    ASSERT_TRUE(u.valid);
    ASSERT_EQ(u.disk_count(), 2u);
    EXPECT_NEAR(u.disks[0].rates.read_bps, 1000.0, kEps);   // disk0: 1000/1
    EXPECT_NEAR(u.disks[0].rates.write_bps, 0.0, kEps);
    EXPECT_NEAR(u.disks[1].rates.read_bps, 0.0, kEps);      // disk1: 無讀
    EXPECT_NEAR(u.disks[1].rates.write_bps, 500.0, kEps);   // disk1: 500/1
}

TEST(UsageFromDelta, EmptyDisksIsInvalid) {
    DiskIoSample prev = makeSample(0.0, {});
    DiskIoSample curr = makeSample(1.0, {});
    DiskIoUsageSample u = usage_from_delta(prev, curr);
    EXPECT_FALSE(u.valid);
    EXPECT_EQ(u.disk_count(), 0u);
}

TEST(UsageFromDelta, CommonDiskCountAlignment) {
    // 磁碟數變動：以共同數（較小者）對齊。
    DiskIoSample prev = makeSample(0.0, {
        makeSnap("disk0", "Disk 0", 0, 0, 0, 0, 0.0),
        makeSnap("disk1", "Disk 1", 0, 0, 0, 0, 0.0),
    });
    DiskIoSample curr = makeSample(1.0, {
        makeSnap("disk0", "Disk 0", 1000, 0, 0, 0, 0.0),
    });
    DiskIoUsageSample u = usage_from_delta(prev, curr);
    ASSERT_TRUE(u.valid);
    EXPECT_EQ(u.disk_count(), 1u);  // min(2,1)
}

TEST(UsageFromDelta, ZeroTimeStillValidWithZeroRates) {
    DiskIoSample prev = makeSample(5.0, {makeSnap("disk0", "Disk 0", 0, 0, 0, 0, 8.0)});
    DiskIoSample curr = makeSample(5.0, {makeSnap("disk0", "Disk 0", 9999, 0, 0, 0, 8.0)});
    DiskIoUsageSample u = usage_from_delta(prev, curr);
    ASSERT_TRUE(u.valid);  // 有共同磁碟
    EXPECT_NEAR(u.disks[0].rates.read_bps, 0.0, kEps);   // dt==0 → 0
    EXPECT_NEAR(u.disks[0].rates.queue_depth, 8.0, kEps);
}

TEST(UsageFromDelta, CounterResetGivesZero) {
    DiskIoSample prev = makeSample(0.0, {makeSnap("disk0", "Disk 0", 9000, 0, 0, 0, 0.0)});
    DiskIoSample curr = makeSample(1.0, {makeSnap("disk0", "Disk 0", 100, 0, 0, 0, 0.0)});
    DiskIoUsageSample u = usage_from_delta(prev, curr);
    ASSERT_TRUE(u.valid);
    EXPECT_NEAR(u.disks[0].rates.read_bps, 0.0, kEps);  // 重置 → 0
}

// ===========================================================================
// NullIoStatSource：注入序列 / 列盡回最後一份
// ===========================================================================
TEST(NullIoStatSource, EmptySequenceReturnsEmpty) {
    NullIoStatSource src;
    EXPECT_TRUE(src.empty());
    DiskIoSample s = src.read();
    EXPECT_TRUE(s.empty());
}

TEST(NullIoStatSource, WalksSequenceThenSticksAtLast) {
    NullIoStatSource src{{
        makeSample(0.0, {makeSnap("disk0", "Disk 0", 0, 0, 0, 0, 0.0)}),
        makeSample(1.0, {makeSnap("disk0", "Disk 0", 1000, 0, 0, 0, 0.0)}),
    }};
    EXPECT_EQ(src.size(), 2u);
    EXPECT_NEAR(src.read().timestamp, 0.0, kEps);
    EXPECT_NEAR(src.read().timestamp, 1.0, kEps);
    EXPECT_NEAR(src.read().timestamp, 1.0, kEps);  // 列盡回最後一份
}

TEST(NullIoStatSource, ResetRewindsCursor) {
    NullIoStatSource src{{
        makeSample(0.0, {makeSnap("disk0", "Disk 0", 0, 0, 0, 0, 0.0)}),
        makeSample(1.0, {makeSnap("disk0", "Disk 0", 1000, 0, 0, 0, 0.0)}),
    }};
    src.read();
    src.read();
    src.reset();
    EXPECT_NEAR(src.read().timestamp, 0.0, kEps);
}

// ===========================================================================
// DifferencingIoRateSource：首次 invalid（需兩份）、之後差分
// ===========================================================================
TEST(DifferencingIoRateSource, FirstSampleIsUnknown) {
    auto stats = std::make_shared<NullIoStatSource>();
    stats->push_sample(makeSample(0.0, {makeSnap("disk0", "Disk 0", 0, 0, 0, 0, 0.0)}));
    stats->push_sample(makeSample(1.0, {makeSnap("disk0", "Disk 0", 1000, 0, 0, 0, 0.0)}));
    DifferencingIoRateSource diff{stats};
    EXPECT_FALSE(diff.primed());
    DiskIoUsageSample first = diff.sample();
    EXPECT_FALSE(first.valid);  // 首次：只有一份，無可差分基準
    EXPECT_TRUE(diff.primed());
    DiskIoUsageSample second = diff.sample();
    ASSERT_TRUE(second.valid);  // 第二次：差分成功
    EXPECT_NEAR(second.disks[0].rates.read_bps, 1000.0, kEps);
}

TEST(DifferencingIoRateSource, NullStatsIsUnknown) {
    DifferencingIoRateSource diff{nullptr};
    EXPECT_FALSE(diff.sample().valid);
}

TEST(DifferencingIoRateSource, ResetRequiresTwoAgain) {
    auto stats = std::make_shared<NullIoStatSource>();
    stats->push_sample(makeSample(0.0, {makeSnap("disk0", "Disk 0", 0, 0, 0, 0, 0.0)}));
    stats->push_sample(makeSample(1.0, {makeSnap("disk0", "Disk 0", 1000, 0, 0, 0, 0.0)}));
    DifferencingIoRateSource diff{stats};
    diff.sample();
    diff.sample();
    diff.reset();
    EXPECT_FALSE(diff.primed());
    EXPECT_FALSE(diff.sample().valid);  // reset 後首次又回未知
}

// ===========================================================================
// DiskIoProvider：註冊五指標、身分、單位、範圍
// ===========================================================================
TEST(DiskIoProvider, RegistersFiveMetrics) {
    MetricRegistry reg;
    DiskIoProvider p{std::make_shared<NullIoRateSource>()};
    p.register_metrics(reg);
    EXPECT_TRUE(reg.contains("disk.io.read_bytes"));
    EXPECT_TRUE(reg.contains("disk.io.write_bytes"));
    EXPECT_TRUE(reg.contains("disk.io.read_iops"));
    EXPECT_TRUE(reg.contains("disk.io.write_iops"));
    EXPECT_TRUE(reg.contains("disk.io.queue"));
    EXPECT_EQ(reg.size(), 5u);
}

TEST(DiskIoProvider, MetricUnitsAndRanges) {
    MetricRegistry reg;
    DiskIoProvider p{std::make_shared<NullIoRateSource>()};
    p.register_metrics(reg);

    auto rb = reg.get("disk.io.read_bytes");
    ASSERT_NE(rb, nullptr);
    EXPECT_EQ(rb->name(), "Disk Read Throughput");
    EXPECT_EQ(rb->unit(), "B/s");
    EXPECT_TRUE(rb->range().has_min());
    EXPECT_FALSE(rb->range().has_max());  // 上無界
    EXPECT_NEAR(*rb->range().min, 0.0, kEps);

    EXPECT_EQ(reg.get("disk.io.read_iops")->unit(), "IOPS");
    EXPECT_EQ(reg.get("disk.io.queue")->unit(), "");
    EXPECT_EQ(reg.get("disk.io.queue")->name(), "Disk Queue Depth");
}

TEST(DiskIoProvider, DuplicateRegistrationRejected) {
    MetricRegistry reg;
    DiskIoProvider p1{std::make_shared<NullIoRateSource>()};
    DiskIoProvider p2{std::make_shared<NullIoRateSource>()};
    p1.register_metrics(reg);
    const std::size_t before = reg.size();
    p2.register_metrics(reg);  // 相同 id → 保守拒絕，不覆寫
    EXPECT_EQ(reg.size(), before);
}

// ===========================================================================
// DiskIoProvider：差分來源下的實例列舉與數值（首次無、第二次有）
// ===========================================================================
TEST(DiskIoProvider, DifferencingSourceFirstSampleNoDisksThenPopulates) {
    auto stats = std::make_shared<NullIoStatSource>();
    stats->push_sample(makeSample(0.0, {makeSnap("disk0", "Disk 0", 0, 0, 0, 0, 2.0)}));
    stats->push_sample(makeSample(2.0, {makeSnap("disk0", "Disk 0", 8000, 4000, 40, 20, 6.0)}));
    auto diff = std::make_shared<DifferencingIoRateSource>(stats);

    MetricRegistry reg;
    DiskIoProvider p{diff};
    p.register_metrics(reg);  // 首次 sample → unknown → 尚無磁碟實例
    EXPECT_EQ(p.disk_count(), 0u);

    p.sample();  // 第二次 → 差分成功 → 磁碟 populated
    EXPECT_EQ(p.disk_count(), 1u);

    auto rb = reg.get("disk.io.read_bytes");
    ASSERT_EQ(rb->instance_count(), 1u);
    const auto& inst = rb->instance(0);
    EXPECT_EQ(inst.instance_id(), "disk0");
    EXPECT_EQ(inst.label(), "Disk 0");
    ASSERT_TRUE(inst.value().valid);
    EXPECT_NEAR(inst.value().number, 4000.0, kEps);  // 8000/2 B/s

    EXPECT_NEAR(reg.get("disk.io.write_bytes")->instance(0).value().number, 2000.0, kEps);
    EXPECT_NEAR(reg.get("disk.io.read_iops")->instance(0).value().number, 20.0, kEps);
    EXPECT_NEAR(reg.get("disk.io.write_iops")->instance(0).value().number, 10.0, kEps);
    EXPECT_NEAR(reg.get("disk.io.queue")->instance(0).value().number, 6.0, kEps);
}

TEST(DiskIoProvider, MultiDiskEnumeration) {
    auto src = makeRateSource({
        DiskIoReading{"disk0", "Disk 0", DiskIoRates{1000, 0, 0, 0, 1.0}},
        DiskIoReading{"disk1", "Disk 1", DiskIoRates{0, 2000, 0, 0, 2.0}},
    });
    MetricRegistry reg;
    DiskIoProvider p{src};
    p.register_metrics(reg);  // 直接速率來源：首次即有值
    EXPECT_EQ(p.disk_count(), 2u);
    auto rb = reg.get("disk.io.read_bytes");
    ASSERT_EQ(rb->instance_count(), 2u);
    EXPECT_EQ(rb->instance(0).instance_id(), "disk0");
    EXPECT_EQ(rb->instance(1).instance_id(), "disk1");
    EXPECT_NEAR(rb->instance(0).value().number, 1000.0, kEps);
    EXPECT_NEAR(reg.get("disk.io.write_bytes")->instance(1).value().number, 2000.0, kEps);
}

TEST(DiskIoProvider, HistoryAccumulatesAcrossSamples) {
    auto src = makeRateSource({
        DiskIoReading{"disk0", "Disk 0", DiskIoRates{1000, 0, 0, 0, 0.0}},
    });
    MetricRegistry reg;
    DiskIoProvider p{src};
    p.register_metrics(reg);  // 第一份推入歷史
    // 換一份新速率再採一次。
    DiskIoUsageSample u2;
    u2.valid = true;
    u2.disks = {DiskIoReading{"disk0", "Disk 0", DiskIoRates{3000, 0, 0, 0, 0.0}}};
    src->set_usage(u2);
    p.sample();

    const auto& inst = reg.get("disk.io.read_bytes")->instance(0);
    EXPECT_EQ(inst.history().size(), 2u);
    EXPECT_NEAR(inst.history().at(0), 1000.0, kEps);  // 最舊
    EXPECT_NEAR(inst.history().at(1), 3000.0, kEps);  // 最新
    EXPECT_NEAR(inst.value().number, 3000.0, kEps);
}

TEST(DiskIoProvider, DisappearedDiskSetUnknownNotPolluteHistory) {
    auto src = makeRateSource({
        DiskIoReading{"disk0", "Disk 0", DiskIoRates{1000, 0, 0, 0, 0.0}},
        DiskIoReading{"disk1", "Disk 1", DiskIoRates{2000, 0, 0, 0, 0.0}},
    });
    MetricRegistry reg;
    DiskIoProvider p{src};
    p.register_metrics(reg);
    EXPECT_EQ(p.disk_count(), 2u);

    // 下一次只剩 disk0（disk1 下線）。
    DiskIoUsageSample u2;
    u2.valid = true;
    u2.disks = {DiskIoReading{"disk0", "Disk 0", DiskIoRates{1500, 0, 0, 0, 0.0}}};
    src->set_usage(u2);
    p.sample();

    // disk1 實例仍在（不移除），但設為未知、歷史不再增長。
    const auto& d1 = reg.get("disk.io.read_bytes")->instance(1);
    EXPECT_EQ(d1.instance_id(), "disk1");
    EXPECT_FALSE(d1.value().valid);
    EXPECT_EQ(d1.history().size(), 1u);  // 只有初次那一份，未被污染
}

// ===========================================================================
// null / 無讀值：誠實 invalid
// ===========================================================================
TEST(DiskIoProvider, NullSourceRegistersButNoDisks) {
    MetricRegistry reg;
    DiskIoProvider p{nullptr};  // 無來源
    p.register_metrics(reg);
    EXPECT_EQ(reg.size(), 5u);       // 五指標仍掛上
    EXPECT_EQ(p.disk_count(), 0u);   // 無磁碟實例（保守，不謊報）
}

TEST(DiskIoProvider, DefaultNullRateSourceIsUnknown) {
    NullIoRateSource src;  // 預設無讀值
    EXPECT_FALSE(src.sample().valid);
    MetricRegistry reg;
    DiskIoProvider p{std::make_shared<NullIoRateSource>()};
    p.register_metrics(reg);
    EXPECT_EQ(p.disk_count(), 0u);
}

TEST(DiskIoProvider, SampleBeforeRegisterIsNoop) {
    DiskIoProvider p{std::make_shared<NullIoRateSource>()};
    p.sample();  // 尚未 register_metrics → 不崩
    EXPECT_EQ(p.disk_count(), 0u);
}

// ===========================================================================
// 經 E2-02 頻率採樣（除頻排程）
// ===========================================================================
TEST(DiskIoProvider, DrivenByE2_02Scheduler) {
    auto stats = std::make_shared<NullIoStatSource>();
    // 累積 read bytes 每 tick +1000，時間戳每 tick +1s → 差分應得 1000 B/s。
    for (int i = 0; i < 6; ++i) {
        stats->push_sample(makeSample(static_cast<double>(i), {
            makeSnap("disk0", "Disk 0", static_cast<std::uint64_t>(i) * 1000, 0, 0, 0, 0.0),
        }));
    }
    auto diff = std::make_shared<DifferencingIoRateSource>(stats);
    MetricRegistry reg;
    DiskIoProvider p{diff};
    p.register_metrics(reg);  // 消耗第一份（prime）

    SamplingScheduler sched;
    const auto id = std::string(DiskIoProvider::kReadBytesId);
    sched.add_demand(id, p.sampling_tier());  // Normal 分級

    // 推進足夠 tick，讓 Normal 間隔觸發數次採集。
    int sampled = 0;
    for (ds::metrics::Tick t = 1; t <= 40; ++t) {
        auto due = sched.advance(t);
        for (const auto& mid : due) {
            if (mid == id) {
                p.sample();
                ++sampled;
            }
        }
    }
    EXPECT_GT(sampled, 0);  // 排程確有觸發
    EXPECT_EQ(p.disk_count(), 1u);
    // 差分穩定得 1000 B/s。
    EXPECT_NEAR(reg.get("disk.io.read_bytes")->instance(0).value().number, 1000.0, kEps);
}

// ===========================================================================
// 消費者只走 E2-01 抽象介面
// ===========================================================================
TEST(DiskIoProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = makeRateSource({
        DiskIoReading{"disk0", "Disk 0", DiskIoRates{2048, 1024, 8, 4, 3.0}},
    });
    MetricRegistry reg;
    DiskIoProvider p{src};
    reg.add_provider(p);  // 經 E2-01 provider 掛載路徑

    // 消費者只透過 Metric / MetricInstance 抽象走訪，不觸及 DiskIoProvider 具體型別。
    std::shared_ptr<Metric> m = reg.get("disk.io.read_bytes");
    ASSERT_NE(m, nullptr);
    ASSERT_TRUE(m->is_single());
    const MetricValue v = m->single().value();
    ASSERT_TRUE(v.valid);
    EXPECT_NEAR(v.number, 2048.0, kEps);
}

// ===========================================================================
// 值模型：相等運算子
// ===========================================================================
TEST(ValueModels, EqualityOperators) {
    EXPECT_EQ((DiskIoCounters{1, 2, 3, 4, 5.0}), (DiskIoCounters{1, 2, 3, 4, 5.0}));
    EXPECT_NE((DiskIoCounters{1, 2, 3, 4, 5.0}), (DiskIoCounters{1, 2, 3, 4, 6.0}));
    EXPECT_EQ((DiskIoRates{1, 2, 3, 4, 5}), (DiskIoRates{1, 2, 3, 4, 5}));
    EXPECT_NE((DiskIoRates{1, 2, 3, 4, 5}), (DiskIoRates{9, 2, 3, 4, 5}));
    EXPECT_EQ(DiskIoUsageSample::unknown(), DiskIoUsageSample::unknown());
}
