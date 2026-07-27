// E2-10 時間與日期 — 測試（gtest）
//
// 覆蓋：曆法分解純算術（epoch 0 / 一般 / 閏日邊界）、三種格式化字串、提供者身分、
// 註冊到 E2-01 registry、注入固定時間 → 三面向（epoch/date/time）值與文字、決定性
// （同注入值兩次結果相同）、時間前進（覆寫來源改變讀值）、null source 保守標未知、
// find_instance、範圍無界、消費者只透過 E2-01 抽象介面走訪、重複註冊保守拒絕。
// 相位 1：只驗介面 + 注入時鐘行為，不含任何平台分支（UTC 純算術）。
#include "time_date.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

#include "metric.hpp"

using ds::metrics::Metric;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRegistry;
using ds::sysinfo::CivilTime;
using ds::sysinfo::civil_from_epoch_seconds;
using ds::sysinfo::FixedTimeSource;
using ds::sysinfo::format_clock;
using ds::sysinfo::format_date;
using ds::sysinfo::format_iso8601;
using ds::sysinfo::TimeDateProvider;
using ds::sysinfo::TimeSource;

namespace {

// 幾個以外部工具（Python）預先算好的正則值（UTC）：
//   epoch 0          -> 1970-01-01T00:00:00Z  days=0      sod=0
//   epoch 1785155696 -> 2026-07-27T12:34:56Z  days=20661  sod=45296
//   epoch 951868799  -> 2000-02-29T23:59:59Z  days=11016  sod=86399（閏日邊界）
constexpr std::int64_t kEpochRef = 1785155696;   // 2026-07-27T12:34:56Z
constexpr std::int64_t kEpochLeap = 951868799;   // 2000-02-29T23:59:59Z

// ===========================================================================
// 曆法分解：純算術，決定性、平台中立
// ===========================================================================
TEST(CivilTime, EpochZeroIsUnixOrigin) {
    const CivilTime c = civil_from_epoch_seconds(0);
    EXPECT_EQ(c.year, 1970);
    EXPECT_EQ(c.month, 1u);
    EXPECT_EQ(c.day, 1u);
    EXPECT_EQ(c.hour, 0u);
    EXPECT_EQ(c.minute, 0u);
    EXPECT_EQ(c.second, 0u);
    EXPECT_EQ(c.days_since_epoch, 0);
    EXPECT_EQ(c.second_of_day, 0);
}

TEST(CivilTime, DecomposesReferenceInstant) {
    const CivilTime c = civil_from_epoch_seconds(kEpochRef);
    EXPECT_EQ(c.year, 2026);
    EXPECT_EQ(c.month, 7u);
    EXPECT_EQ(c.day, 27u);
    EXPECT_EQ(c.hour, 12u);
    EXPECT_EQ(c.minute, 34u);
    EXPECT_EQ(c.second, 56u);
    EXPECT_EQ(c.epoch_seconds, kEpochRef);
    EXPECT_EQ(c.days_since_epoch, 20661);
    EXPECT_EQ(c.second_of_day, 45296);
}

// 閏日邊界（2000-02-29 為閏日）+ 一日最後一秒。
TEST(CivilTime, HandlesLeapDayBoundary) {
    const CivilTime c = civil_from_epoch_seconds(kEpochLeap);
    EXPECT_EQ(c.year, 2000);
    EXPECT_EQ(c.month, 2u);
    EXPECT_EQ(c.day, 29u);
    EXPECT_EQ(c.hour, 23u);
    EXPECT_EQ(c.minute, 59u);
    EXPECT_EQ(c.second, 59u);
    EXPECT_EQ(c.second_of_day, 86399);
}

// ===========================================================================
// 格式化：固定寬度、零外部相依
// ===========================================================================
TEST(Format, ProducesFixedWidthStrings) {
    const CivilTime c = civil_from_epoch_seconds(kEpochRef);
    EXPECT_EQ(format_iso8601(c), "2026-07-27T12:34:56Z");
    EXPECT_EQ(format_date(c), "2026-07-27");
    EXPECT_EQ(format_clock(c), "12:34:56");
}

TEST(Format, ZeroPadsEpochOrigin) {
    const CivilTime c = civil_from_epoch_seconds(0);
    EXPECT_EQ(format_iso8601(c), "1970-01-01T00:00:00Z");
    EXPECT_EQ(format_date(c), "1970-01-01");
    EXPECT_EQ(format_clock(c), "00:00:00");
}

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(TimeDateProvider, ProviderIdIsStable) {
    TimeDateProvider p{std::make_shared<FixedTimeSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.time");
    EXPECT_EQ(std::string(TimeDateProvider::kMetricId), "time.now");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(TimeDateProvider, IsAMetricProvider) {
    auto p = std::make_shared<TimeDateProvider>(std::make_shared<FixedTimeSource>());
    MetricProvider* base = p.get();  // 可上轉為 E2-01 抽象介面
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->provider_id(), "sysinfo.time");
}

// ===========================================================================
// 註冊到 E2-01 registry
// ===========================================================================
TEST(TimeDateProvider, RegistersMetricViaRegistry) {
    MetricRegistry registry;
    TimeDateProvider provider{std::make_shared<FixedTimeSource>(kEpochRef)};

    const std::size_t added = registry.add_provider(provider);
    EXPECT_EQ(added, 1u);  // 掛上一個指標
    EXPECT_TRUE(registry.contains("time.now"));
    EXPECT_EQ(registry.size(), 1u);

    auto metric = registry.get("time.now");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->name(), "Current Time & Date");
    EXPECT_EQ(metric->unit(), "");
    EXPECT_EQ(metric->instance_count(), 3u);  // epoch / date / time
}

// ===========================================================================
// 注入固定時間 → 三面向值與文字（決定性）
// ===========================================================================
TEST(TimeDateProvider, InjectedTimeYieldsDeterministicValues) {
    MetricRegistry registry;
    TimeDateProvider provider{std::make_shared<FixedTimeSource>(kEpochRef)};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("time.now");
    ASSERT_NE(metric, nullptr);

    // 全程只用 E2-01 抽象 Metric / MetricInstance 介面，不觸及 sysinfo 具體型別。
    const auto* epoch = metric->find_instance("epoch");
    const auto* date = metric->find_instance("date");
    const auto* time = metric->find_instance("time");
    ASSERT_NE(epoch, nullptr);
    ASSERT_NE(date, nullptr);
    ASSERT_NE(time, nullptr);

    // epoch 面向：number = epoch 秒、text = ISO-8601。
    const auto ev = epoch->value();
    EXPECT_TRUE(ev.valid);
    EXPECT_DOUBLE_EQ(ev.number, static_cast<double>(kEpochRef));
    ASSERT_TRUE(ev.text.has_value());
    EXPECT_EQ(*ev.text, "2026-07-27T12:34:56Z");

    // date 面向：number = 曆日數、text = "YYYY-MM-DD"。
    const auto dv = date->value();
    EXPECT_TRUE(dv.valid);
    EXPECT_DOUBLE_EQ(dv.number, 20661.0);
    ASSERT_TRUE(dv.text.has_value());
    EXPECT_EQ(*dv.text, "2026-07-27");

    // time 面向：number = 日內秒、text = "HH:MM:SS"。
    const auto tv = time->value();
    EXPECT_TRUE(tv.valid);
    EXPECT_DOUBLE_EQ(tv.number, 45296.0);
    ASSERT_TRUE(tv.text.has_value());
    EXPECT_EQ(*tv.text, "12:34:56");
}

// 同注入值 → 兩個獨立提供者結果一致（決定性、不讀真實時鐘）。
TEST(TimeDateProvider, SameInjectedInstantIsReproducible) {
    MetricRegistry r1;
    MetricRegistry r2;
    TimeDateProvider p1{std::make_shared<FixedTimeSource>(kEpochRef)};
    TimeDateProvider p2{std::make_shared<FixedTimeSource>(kEpochRef)};
    ASSERT_EQ(r1.add_provider(p1), 1u);
    ASSERT_EQ(r2.add_provider(p2), 1u);

    const auto t1 = *r1.get("time.now")->find_instance("epoch")->value().text;
    const auto t2 = *r2.get("time.now")->find_instance("epoch")->value().text;
    EXPECT_EQ(t1, t2);
    EXPECT_EQ(t1, "2026-07-27T12:34:56Z");
}

// 時間前進：覆寫來源改變讀值（注入式時鐘可控）。
TEST(TimeDateProvider, AdvancingClockChangesReading) {
    auto src = std::make_shared<FixedTimeSource>(0);
    {
        MetricRegistry registry;
        TimeDateProvider provider{src};
        ASSERT_EQ(registry.add_provider(provider), 1u);
        EXPECT_EQ(*registry.get("time.now")->find_instance("date")->value().text,
                  "1970-01-01");
    }
    // 把時間推進到參考時刻，重新註冊 → 讀值隨之改變。
    src->set_epoch_seconds(kEpochRef);
    {
        MetricRegistry registry;
        TimeDateProvider provider{src};
        ASSERT_EQ(registry.add_provider(provider), 1u);
        EXPECT_EQ(*registry.get("time.now")->find_instance("date")->value().text,
                  "2026-07-27");
    }
}

// ===========================================================================
// null source 保守：仍掛指標、三實例標未知（不謊報 0）
// ===========================================================================
TEST(TimeDateProvider, NullSourceMarksInstancesUnknown) {
    MetricRegistry registry;
    TimeDateProvider provider{nullptr};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    auto metric = registry.get("time.now");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 3u);
    for (std::size_t i = 0; i < metric->instance_count(); ++i) {
        EXPECT_FALSE(metric->instance(i).value().valid);  // 未知，非 0 讀值
    }
}

// find_instance 未知 id → nullptr（E2-01 便利查詢，走抽象介面）。
TEST(TimeDateProvider, FindInstanceUnknownReturnsNull) {
    MetricRegistry registry;
    TimeDateProvider provider{std::make_shared<FixedTimeSource>(kEpochRef)};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("time.now");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->find_instance("does.not.exist"), nullptr);
}

// 範圍無界（時間軸無上下界）。
TEST(TimeDateProvider, MetricRangeIsUnbounded) {
    MetricRegistry registry;
    TimeDateProvider provider{std::make_shared<FixedTimeSource>(kEpochRef)};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    const auto r = registry.get("time.now")->range();
    EXPECT_FALSE(r.has_min());
    EXPECT_FALSE(r.has_max());
    EXPECT_FALSE(r.is_bounded());
}

// ===========================================================================
// FixedTimeSource 注入 API
// ===========================================================================
TEST(FixedTimeSource, DefaultsToEpochOrigin) {
    FixedTimeSource src;  // 預設 = epoch
    EXPECT_EQ(src.now_epoch_seconds(), 0);
}

TEST(FixedTimeSource, ConstructAndOverride) {
    FixedTimeSource src{kEpochRef};
    EXPECT_EQ(src.now_epoch_seconds(), kEpochRef);
    src.set_epoch_seconds(0);
    EXPECT_EQ(src.now_epoch_seconds(), 0);
}

// 抽象 TimeSource：提供者只依賴此介面（可注入任意實作）。
TEST(FixedTimeSource, IsATimeSource) {
    auto src = std::make_shared<FixedTimeSource>(kEpochRef);
    TimeSource* base = src.get();  // 可上轉為抽象時鐘介面
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->now_epoch_seconds(), kEpochRef);
}

// ===========================================================================
// 消費者範式：新增指標 = 新增提供者、消費者只走 E2-01 registry
// ===========================================================================
TEST(TimeDateProvider, ConsumerWalksViaAbstractContractOnly) {
    MetricRegistry registry;
    TimeDateProvider provider{std::make_shared<FixedTimeSource>(kEpochRef)};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    // 一個「掛件」風格的消費者：只認得 E2-01 的 Metric，數面向數量。
    std::size_t facets = 0;
    for (const auto& m : registry.all()) {
        facets += m->instance_count();  // 全程無 sysinfo 型別
    }
    EXPECT_EQ(facets, 3u);
}

// 重複註冊保守拒絕（同 id 第二次掛上失敗、不覆寫）。
TEST(TimeDateProvider, DuplicateRegistrationRejected) {
    MetricRegistry registry;
    TimeDateProvider p1{std::make_shared<FixedTimeSource>(kEpochRef)};
    TimeDateProvider p2{std::make_shared<FixedTimeSource>(0)};

    EXPECT_EQ(registry.add_provider(p1), 1u);
    EXPECT_EQ(registry.add_provider(p2), 0u);  // 同 id "time.now" 被拒
    // 既有指標未被覆寫：仍為 p1 注入時刻。
    EXPECT_EQ(*registry.get("time.now")->find_instance("date")->value().text, "2026-07-27");
}

}  // namespace
