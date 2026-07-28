// E2-11 系統運行時間 — 測試（gtest）
//
// 覆蓋：提供者身分、註冊到 E2-01 registry、uptime 實例列舉與讀值（秒數）、
// 格式化文字（format_uptime "Nd HH:MM:SS"）、遞增序列（模擬時間推進，歷史累積）、
// 可選開機時間戳實例（有/無、動態新增）、無讀值誠實 invalid、經 E2-02 頻率採樣（除頻排程）、
// null 來源行為（固定/序列/預設未知）、範圍 at_least(0)、消費者只走 E2-01 抽象、重複註冊。
// 相位 1：只驗介面 + 注入式來源行為，不含任何平台分支 / 真實時間 API。
#include "uptime.hpp"

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
using ds::sysinfo::format_uptime;
using ds::sysinfo::NullUptimeSource;
using ds::sysinfo::UptimeProvider;
using ds::sysinfo::UptimeReading;
using ds::sysinfo::UptimeSource;

namespace {

constexpr double kEps = 1e-9;

// 固定 uptime 來源（僅秒數）。
std::shared_ptr<NullUptimeSource> makeFixed(double secs) {
    auto src = std::make_shared<NullUptimeSource>();
    src->set_seconds(secs);
    return src;
}

}  // namespace

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(UptimeProvider, ProviderIdIsStable) {
    UptimeProvider p{std::make_shared<NullUptimeSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.uptime");
    EXPECT_EQ(std::string(UptimeProvider::kMetricId), "system.uptime");
    EXPECT_EQ(std::string(UptimeProvider::kMetricName), "System Uptime");
    EXPECT_EQ(std::string(UptimeProvider::kUnit), "s");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(UptimeProvider, IsMetricProvider) {
    UptimeProvider p{std::make_shared<NullUptimeSource>()};
    MetricProvider& mp = p;  // 可上轉
    EXPECT_EQ(mp.provider_id(), "sysinfo.uptime");
}

// 預設採集分級 = Low（uptime 變動緩慢），可由建構子覆寫。
TEST(UptimeProvider, SamplingTierDefaultsLowOverridable) {
    UptimeProvider def{std::make_shared<NullUptimeSource>()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::Low);

    UptimeProvider high{std::make_shared<NullUptimeSource>(),
                        UptimeProvider::kDefaultHistory, SamplingTier::High};
    EXPECT_EQ(high.sampling_tier(), SamplingTier::High);
}

// ===========================================================================
// 註冊 / 列舉 / uptime 秒數讀值
// ===========================================================================
TEST(UptimeProvider, RegistersSingleUptimeMetric) {
    auto src = makeFixed(3661.0);  // 1h 1m 1s
    UptimeProvider p{src};
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);

    EXPECT_EQ(added, 1u);
    EXPECT_TRUE(reg.contains("system.uptime"));
    auto m = reg.get("system.uptime");
    ASSERT_NE(m, nullptr);

    // 無開機時間戳 → 只有 uptime 實例。
    EXPECT_EQ(m->instance_count(), 1u);
    EXPECT_EQ(m->instance(0).instance_id(), "uptime");
    EXPECT_EQ(m->instance(0).label(), "System Uptime");
    EXPECT_FALSE(p.has_boot_time());
}

// uptime 秒數讀值正確（number = 秒數）。
TEST(UptimeProvider, UptimeSecondsValue) {
    auto src = makeFixed(90061.0);  // 1d 1h 1m 1s
    UptimeProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("system.uptime");
    const auto* up = m->find_instance("uptime");
    ASSERT_NE(up, nullptr);
    ASSERT_TRUE(up->value().valid);
    EXPECT_NEAR(up->value().number, 90061.0, kEps);
}

// ===========================================================================
// 格式化文字
// ===========================================================================
TEST(FormatUptime, DaysHoursMinutesSeconds) {
    EXPECT_EQ(format_uptime(0.0), "0d 00:00:00");
    EXPECT_EQ(format_uptime(42.0), "0d 00:00:42");
    EXPECT_EQ(format_uptime(61.0), "0d 00:01:01");
    EXPECT_EQ(format_uptime(3661.0), "0d 01:01:01");
    EXPECT_EQ(format_uptime(86400.0), "1d 00:00:00");
    EXPECT_EQ(format_uptime(90061.0), "1d 01:01:01");
    EXPECT_EQ(format_uptime(864000.0), "10d 00:00:00");  // 天數可任意位數
}

// 格式化邊界：負值視為 0、小數秒截斷。
TEST(FormatUptime, NegativeAndFractional) {
    EXPECT_EQ(format_uptime(-5.0), "0d 00:00:00");  // 負值保守視為 0
    EXPECT_EQ(format_uptime(42.9), "0d 00:00:42");  // 小數截斷取整
}

// 提供者把格式化文字掛在 value().text 上。
TEST(UptimeProvider, FormattedTextInValue) {
    auto src = makeFixed(90061.0);
    UptimeProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("system.uptime");
    const auto* up = m->find_instance("uptime");
    ASSERT_TRUE(up->value().valid);
    ASSERT_TRUE(up->value().text.has_value());
    EXPECT_EQ(*up->value().text, "1d 01:01:01");
}

// ===========================================================================
// 遞增序列（模擬時間推進，歷史累積）
// ===========================================================================
TEST(UptimeProvider, IncrementingSequenceAccumulatesHistory) {
    // 每次 +1 秒的遞增序列。
    auto src = std::make_shared<NullUptimeSource>(std::vector<UptimeReading>{
        UptimeReading::of(100.0),
        UptimeReading::of(101.0),
        UptimeReading::of(102.0),
        UptimeReading::of(103.0),
    });
    UptimeProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // register 消耗第 0 份（100）並推入歷史
    auto m = reg.get("system.uptime");
    const auto* up = m->find_instance("uptime");

    EXPECT_NEAR(up->value().number, 100.0, kEps);
    EXPECT_EQ(up->history().size(), 1u);

    p.sample();  // 101
    p.sample();  // 102
    p.sample();  // 103
    EXPECT_NEAR(up->value().number, 103.0, kEps);
    ASSERT_EQ(up->history().size(), 4u);
    // 歷史時序：最舊=100 … 最新=103（單調遞增）。
    EXPECT_NEAR(up->history().at(0), 100.0, kEps);
    EXPECT_NEAR(up->history().latest(), 103.0, kEps);
}

// NullUptimeSource 序列：列盡持續回最後一份。
TEST(NullUptimeSource, SequenceExhaustionReturnsLast) {
    NullUptimeSource src{std::vector<UptimeReading>{
        UptimeReading::of(10.0),
        UptimeReading::of(20.0),
    }};
    EXPECT_NEAR(src.read().seconds, 10.0, kEps);
    EXPECT_NEAR(src.read().seconds, 20.0, kEps);
    // 列盡 → 持續回最後一份。
    EXPECT_NEAR(src.read().seconds, 20.0, kEps);
    EXPECT_NEAR(src.read().seconds, 20.0, kEps);
}

// NullUptimeSource：reset 後游標回起點。
TEST(NullUptimeSource, ResetRewindsCursor) {
    NullUptimeSource src{std::vector<UptimeReading>{
        UptimeReading::of(10.0),
        UptimeReading::of(20.0),
    }};
    EXPECT_NEAR(src.read().seconds, 10.0, kEps);
    src.reset();
    EXPECT_NEAR(src.read().seconds, 10.0, kEps);  // 回起點
}

// ===========================================================================
// 開機時間戳（可選實例）
// ===========================================================================
TEST(UptimeProvider, BootTimestampCreatesOptionalInstance) {
    // 讀值帶開機時間戳。
    auto src = std::make_shared<NullUptimeSource>();
    src->set_reading(UptimeReading::of(500.0, 1700000000.0));  // uptime 500s、boot ts
    UptimeProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("system.uptime");

    // uptime + boot.time = 2 實例（uptime 在前）。
    EXPECT_EQ(m->instance_count(), 2u);
    EXPECT_TRUE(p.has_boot_time());
    EXPECT_EQ(m->instance(0).instance_id(), "uptime");
    const auto* boot = m->find_instance("boot.time");
    ASSERT_NE(boot, nullptr);
    EXPECT_EQ(boot->label(), "Boot Time");
    ASSERT_TRUE(boot->value().valid);
    EXPECT_NEAR(boot->value().number, 1700000000.0, kEps);
}

// 開機時間戳實例可跨採樣動態新增（首次無、後來有）。
TEST(UptimeProvider, BootTimestampAddedDynamically) {
    auto src = std::make_shared<NullUptimeSource>();
    src->set_seconds(100.0);  // 首次僅秒數，無開機時間戳
    UptimeProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("system.uptime");

    EXPECT_EQ(m->instance_count(), 1u);  // 僅 uptime
    EXPECT_FALSE(p.has_boot_time());
    const auto* up_before = m->find_instance("uptime");

    // 之後來源帶上開機時間戳 → sample() 後動態新增 boot.time 實例。
    src->set_reading(UptimeReading::of(101.0, 1700000000.0));
    p.sample();
    EXPECT_EQ(m->instance_count(), 2u);
    EXPECT_TRUE(p.has_boot_time());
    // 既有 uptime 參照仍有效（unique_ptr 持有實例）。
    EXPECT_EQ(m->find_instance("uptime"), up_before);
    EXPECT_NEAR(m->find_instance("boot.time")->value().number, 1700000000.0, kEps);
}

// 開機時間戳實例不保留歷史（靜態值）。
TEST(UptimeProvider, BootTimestampHasNoHistory) {
    auto src = std::make_shared<NullUptimeSource>();
    src->set_reading(UptimeReading::of(500.0, 1700000000.0));
    UptimeProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("system.uptime");
    const auto* boot = m->find_instance("boot.time");
    ASSERT_NE(boot, nullptr);
    EXPECT_EQ(boot->history().capacity(), 0u);  // 無歷史
    EXPECT_TRUE(boot->history().empty());
}

// ===========================================================================
// 無讀值誠實 invalid
// ===========================================================================
TEST(UptimeProvider, NullSourceIsConservative) {
    // source 為 null 指標：仍掛上指標，uptime 未知、無 boot 實例、不崩。
    UptimeProvider p{nullptr};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 1u);
    auto m = reg.get("system.uptime");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->instance_count(), 1u);
    EXPECT_FALSE(m->find_instance("uptime")->value().valid);  // 未知
    EXPECT_FALSE(p.has_boot_time());
    // sample() 不崩。
    p.sample();
    EXPECT_FALSE(m->find_instance("uptime")->value().valid);
}

// NullUptimeSource 預設（未注入）→ 無讀值；uptime 實例 invalid、不推歷史。
TEST(UptimeProvider, DefaultSourceUnknownNoHistory) {
    auto src = std::make_shared<NullUptimeSource>();  // 預設 unknown
    UptimeProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("system.uptime");
    const auto* up = m->find_instance("uptime");
    EXPECT_FALSE(up->value().valid);
    EXPECT_EQ(up->history().size(), 0u);  // 無讀值不污染歷史

    // 之後注入 → sample() 後可讀且推入歷史。
    src->set_seconds(200.0);
    p.sample();
    EXPECT_TRUE(up->value().valid);
    EXPECT_NEAR(up->value().number, 200.0, kEps);
    EXPECT_EQ(up->history().size(), 1u);
}

// 曾有開機時間戳、後續讀值無 → boot 實例設未知（不縮減、誠實表達本次無讀值）。
TEST(UptimeProvider, BootTimestampGoesUnknownWhenMissing) {
    auto src = std::make_shared<NullUptimeSource>();
    src->set_reading(UptimeReading::of(100.0, 1700000000.0));
    UptimeProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("system.uptime");
    EXPECT_TRUE(m->find_instance("boot.time")->value().valid);

    // 本次讀值只有秒數（無開機時間戳）。
    src->set_seconds(101.0);
    p.sample();
    EXPECT_EQ(m->instance_count(), 2u);  // 不縮減
    EXPECT_FALSE(m->find_instance("boot.time")->value().valid);  // 未知
    // uptime 仍有效前進。
    EXPECT_NEAR(m->find_instance("uptime")->value().number, 101.0, kEps);
}

// sample() 未 register_metrics 時為 no-op（不崩）。
TEST(UptimeProvider, SampleBeforeRegisterIsNoop) {
    UptimeProvider p{makeFixed(100.0)};
    p.sample();  // 尚未 register → no-op，不崩
    EXPECT_FALSE(p.has_boot_time());
}

// ===========================================================================
// 經 E2-02 頻率採樣（除頻排程）
// ===========================================================================
TEST(UptimeProvider, SampledViaE2_02Scheduler) {
    // 每次 +1 秒的遞增序列（含首份基準 + 之後多份）。
    std::vector<UptimeReading> seq;
    for (int k = 0; k <= 40; ++k) {
        seq.push_back(UptimeReading::of(1000.0 + k));
    }
    auto src = std::make_shared<NullUptimeSource>(std::move(seq));
    UptimeProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // register 消耗第 0 份（1000）並推歷史

    SamplingScheduler sched{};  // 預設 policy：Low 間隔 = 64
    sched.add_demand(UptimeProvider::kMetricId, p.sampling_tier());  // Low
    ASSERT_TRUE(sched.effective_tier(UptimeProvider::kMetricId).has_value());
    EXPECT_EQ(*sched.effective_tier(UptimeProvider::kMetricId), SamplingTier::Low);

    // 推進 192 個 tick，Low 間隔 64 → 於 t=64,128,192 到期共 3 次採集。
    int sampled = 0;
    for (ds::metrics::Tick t = 1; t <= 192; ++t) {
        auto due = sched.advance(t);
        for (const auto& id : due) {
            if (id == UptimeProvider::kMetricId) {
                p.sample();
                ++sampled;
            }
        }
    }
    EXPECT_EQ(sampled, 3);

    auto m = reg.get("system.uptime");
    const auto& hist = m->find_instance("uptime")->history();
    // register 首採（1000）+ 3 次排程採集 = 歷史累積 4 筆，單調遞增。
    EXPECT_EQ(hist.size(), 4u);
    EXPECT_NEAR(hist.at(0), 1000.0, kEps);
    EXPECT_NEAR(hist.latest(), 1003.0, kEps);
}

// 除頻：多消費者同一指標合併，最高頻者供給。
TEST(UptimeProvider, DeFrequencyCoalescesDemands) {
    UptimeProvider p{makeFixed(100.0)};
    SamplingScheduler sched{};
    auto d_low = sched.add_demand(UptimeProvider::kMetricId, SamplingTier::Low);
    sched.add_demand(UptimeProvider::kMetricId, SamplingTier::High);
    // 有效分級 = 最高頻者 High。
    ASSERT_TRUE(sched.effective_tier(UptimeProvider::kMetricId).has_value());
    EXPECT_EQ(*sched.effective_tier(UptimeProvider::kMetricId), SamplingTier::High);
    EXPECT_EQ(sched.demand_count(UptimeProvider::kMetricId), 2u);

    // 撤銷 Low 需求後仍被 High 追蹤。
    EXPECT_TRUE(sched.remove_demand(d_low));  // 撤 Low
    EXPECT_TRUE(sched.tracks(UptimeProvider::kMetricId));
    EXPECT_EQ(*sched.effective_tier(UptimeProvider::kMetricId), SamplingTier::High);
}

// ===========================================================================
// 範圍 / 消費者範式 / 重複註冊
// ===========================================================================
TEST(UptimeProvider, RangeIsAtLeastZeroUnbounded) {
    UptimeProvider p{makeFixed(100.0)};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("system.uptime");
    auto r = m->range();
    // 下界 0、上無界（uptime 只增，無自然上限）→ 非有界。
    EXPECT_TRUE(r.has_min());
    EXPECT_FALSE(r.has_max());
    EXPECT_FALSE(r.is_bounded());
    EXPECT_NEAR(*r.min, 0.0, kEps);
    // 無上界 → 無法正規化。
    EXPECT_FALSE(r.normalized(100.0).has_value());
}

// 掛件風格消費者：只透過 E2-01 registry / Metric 抽象介面走訪，完全不觸及具體型別。
TEST(UptimeProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = std::make_shared<NullUptimeSource>();
    src->set_reading(UptimeReading::of(3661.0, 1700000000.0));
    UptimeProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    // 消費者只認識 MetricRegistry + Metric + MetricInstance。
    std::shared_ptr<Metric> m = reg.get("system.uptime");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), "s");
    const auto& up = m->instance(0);
    EXPECT_EQ(up.instance_id(), "uptime");
    ASSERT_TRUE(up.value().valid);
    EXPECT_NEAR(up.value().number, 3661.0, kEps);
    ASSERT_TRUE(up.value().text.has_value());
    EXPECT_EQ(*up.value().text, "0d 01:01:01");
}

TEST(UptimeProvider, DuplicateRegistrationRejected) {
    UptimeProvider p1{makeFixed(100.0)};
    UptimeProvider p2{makeFixed(200.0)};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 1u);
    // 第二個提供者掛同一 id → 保守拒絕（不覆寫既有）。
    EXPECT_EQ(reg.add_provider(p2), 0u);
    EXPECT_EQ(reg.size(), 1u);
}

// ===========================================================================
// 值模型單元
// ===========================================================================
TEST(UptimeReading, FactoriesAndEquality) {
    UptimeReading u = UptimeReading::unknown();
    EXPECT_FALSE(u.valid);
    EXPECT_FALSE(u.boot_unix_time.has_value());

    UptimeReading a = UptimeReading::of(100.0);
    EXPECT_TRUE(a.valid);
    EXPECT_NEAR(a.seconds, 100.0, kEps);
    EXPECT_FALSE(a.boot_unix_time.has_value());

    UptimeReading b = UptimeReading::of(100.0, 1700000000.0);
    ASSERT_TRUE(b.boot_unix_time.has_value());
    EXPECT_NEAR(*b.boot_unix_time, 1700000000.0, kEps);
    EXPECT_NE(a, b);
    EXPECT_EQ(a, UptimeReading::of(100.0));
}

TEST(NullUptimeSource, FixedInjectAndClear) {
    NullUptimeSource src;
    EXPECT_FALSE(src.read().valid);  // 預設未知
    src.set_seconds(50.0);
    UptimeReading r = src.read();
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.seconds, 50.0, kEps);
    src.clear();
    EXPECT_FALSE(src.read().valid);  // 回到未知
}

// 序列優先於固定值；clear 後兩者皆重置。
TEST(NullUptimeSource, SequenceTakesPriorityOverFixed) {
    NullUptimeSource src;
    src.set_seconds(999.0);  // 固定值
    src.set_sequence({UptimeReading::of(10.0)});  // 序列
    EXPECT_NEAR(src.read().seconds, 10.0, kEps);   // 序列優先
    EXPECT_NEAR(src.read().seconds, 10.0, kEps);   // 列盡回最後一份
    src.clear();
    EXPECT_TRUE(src.empty());
    EXPECT_FALSE(src.read().valid);  // 序列清空、固定值 unknown
}
