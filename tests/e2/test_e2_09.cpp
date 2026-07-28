// E2-09 電源與電池狀態 — 測試（gtest）
//
// 覆蓋：提供者身分、註冊到 E2-01 registry（六個單一實例指標）、電量 %、充電狀態轉換
// （放電→充電→滿，經序列來源）、是否接電源、剩餘時間估計、循環次數 / 健康度（可選）、
// 無電池(桌機)情境、0 / 100 電量邊界、無讀值 invalid（所有指標未知）、經 E2-02 採樣（除頻
// 排程 + 歷史累積）、null 來源行為、序列來源列盡語意、範圍 bounded / at_least、消費者只走
// E2-01 抽象介面、重複註冊保守拒絕。相位 1：只驗介面 + 注入式來源行為，不含任何平台分支。
#include "power_status.hpp"

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
using ds::sysinfo::ChargeState;
using ds::sysinfo::NullPowerStatSource;
using ds::sysinfo::PowerProvider;
using ds::sysinfo::PowerStatSource;
using ds::sysinfo::PowerStatus;
using ds::sysinfo::SequencedPowerStatSource;
using ds::sysinfo::charge_state_code;
using ds::sysinfo::charge_state_label;
using ds::sysinfo::to_string;

namespace {

constexpr double kEps = 1e-9;

// 固定狀態來源便利工廠。
std::shared_ptr<NullPowerStatSource> makeFixed(PowerStatus s) {
    return std::make_shared<NullPowerStatSource>(std::move(s));
}

}  // namespace

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(PowerProvider, ProviderIdAndMetricIdsStable) {
    PowerProvider p{std::make_shared<NullPowerStatSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.power");
    EXPECT_EQ(std::string(PowerProvider::kLevelId), "power.battery.level");
    EXPECT_EQ(std::string(PowerProvider::kStateId), "power.battery.state");
    EXPECT_EQ(std::string(PowerProvider::kAcId), "power.ac.online");
    EXPECT_EQ(std::string(PowerProvider::kTimeId), "power.battery.time_remaining");
    EXPECT_EQ(std::string(PowerProvider::kCyclesId), "power.battery.cycles");
    EXPECT_EQ(std::string(PowerProvider::kHealthId), "power.battery.health");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(PowerProvider, IsMetricProvider) {
    PowerProvider p{std::make_shared<NullPowerStatSource>()};
    MetricProvider& mp = p;  // 可上轉
    EXPECT_EQ(mp.provider_id(), "sysinfo.power");
}

// 預設採集分級 = Low（電源變動慢），可由建構子覆寫。
TEST(PowerProvider, SamplingTierDefaultsLowOverridable) {
    PowerProvider def{std::make_shared<NullPowerStatSource>()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::Low);

    PowerProvider hi{std::make_shared<NullPowerStatSource>(),
                     PowerProvider::kDefaultHistory, SamplingTier::Normal};
    EXPECT_EQ(hi.sampling_tier(), SamplingTier::Normal);
}

// ===========================================================================
// 註冊 / 列舉：六個單一實例指標
// ===========================================================================
TEST(PowerProvider, RegistersSixSingleInstanceMetrics) {
    auto src = makeFixed(PowerStatus::battery(80.0, ChargeState::Discharging,
                                              /*on_ac=*/false, /*minutes=*/120));
    PowerProvider p{src};
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);

    EXPECT_EQ(added, 6u);
    EXPECT_TRUE(reg.contains("power.battery.level"));
    EXPECT_TRUE(reg.contains("power.battery.state"));
    EXPECT_TRUE(reg.contains("power.ac.online"));
    EXPECT_TRUE(reg.contains("power.battery.time_remaining"));
    EXPECT_TRUE(reg.contains("power.battery.cycles"));
    EXPECT_TRUE(reg.contains("power.battery.health"));

    // 各為單一實例（kSingleInstanceId=""）。
    auto lvl = reg.get("power.battery.level");
    ASSERT_NE(lvl, nullptr);
    EXPECT_EQ(lvl->instance_count(), 1u);
    EXPECT_TRUE(lvl->is_single());
    EXPECT_EQ(lvl->instance(0).instance_id(), std::string(Metric::kSingleInstanceId));
}

// ===========================================================================
// 電量 %
// ===========================================================================
TEST(PowerProvider, BatteryLevelPercent) {
    auto src = makeFixed(PowerStatus::battery(63.5, ChargeState::Discharging, false));
    PowerProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    auto lvl = reg.get("power.battery.level");
    ASSERT_NE(lvl, nullptr);
    const auto& v = lvl->single().value();
    ASSERT_TRUE(v.valid);
    EXPECT_NEAR(v.number, 63.5, kEps);
    EXPECT_EQ(lvl->unit(), "%");
}

// 0% / 100% 邊界。
TEST(PowerProvider, ZeroAndHundredPercentBoundaries) {
    {
        auto p0 = makeFixed(PowerStatus::battery(0.0, ChargeState::Discharging, false));
        PowerProvider p{p0};
        MetricRegistry reg;
        reg.add_provider(p);
        EXPECT_NEAR(reg.get("power.battery.level")->single().value().number, 0.0, kEps);
    }
    {
        auto p100 = makeFixed(PowerStatus::battery(100.0, ChargeState::Full, true));
        PowerProvider p{p100};
        MetricRegistry reg;
        reg.add_provider(p);
        EXPECT_NEAR(reg.get("power.battery.level")->single().value().number, 100.0, kEps);
    }
}

// ===========================================================================
// 充電狀態（碼 + 文字）與序列轉換
// ===========================================================================
TEST(ChargeStateModel, CodeAndLabelAndStableString) {
    EXPECT_NEAR(charge_state_code(ChargeState::NoBattery), 0.0, kEps);
    EXPECT_NEAR(charge_state_code(ChargeState::Discharging), 1.0, kEps);
    EXPECT_NEAR(charge_state_code(ChargeState::Charging), 2.0, kEps);
    EXPECT_NEAR(charge_state_code(ChargeState::Full), 3.0, kEps);

    EXPECT_EQ(charge_state_label(ChargeState::Charging), "Charging");
    EXPECT_EQ(charge_state_label(ChargeState::NoBattery), "No Battery");

    EXPECT_STREQ(to_string(ChargeState::Discharging), "discharging");
    EXPECT_STREQ(to_string(ChargeState::Full), "full");
}

TEST(PowerProvider, StateMetricCarriesCodeAndText) {
    auto src = makeFixed(PowerStatus::battery(50.0, ChargeState::Charging, true));
    PowerProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    auto st = reg.get("power.battery.state");
    ASSERT_NE(st, nullptr);
    const auto& v = st->single().value();
    ASSERT_TRUE(v.valid);
    EXPECT_NEAR(v.number, charge_state_code(ChargeState::Charging), kEps);
    ASSERT_TRUE(v.text.has_value());
    EXPECT_EQ(*v.text, "Charging");
    EXPECT_EQ(st->unit(), "");
}

// 充電狀態轉換：放電 → 充電 → 滿（經序列來源，逐次 sample 前進）。
TEST(PowerProvider, ChargingStateTransitionsAcrossSamples) {
    auto seq = std::make_shared<SequencedPowerStatSource>(std::vector<PowerStatus>{
        PowerStatus::battery(40.0, ChargeState::Discharging, false, 90),
        PowerStatus::battery(45.0, ChargeState::Charging, true, 60),
        PowerStatus::battery(100.0, ChargeState::Full, true),
    });
    PowerProvider p{seq};
    MetricRegistry reg;
    reg.add_provider(p);  // 消耗第 0 份：放電
    auto st = reg.get("power.battery.state");
    auto lvl = reg.get("power.battery.level");

    EXPECT_NEAR(st->single().value().number, charge_state_code(ChargeState::Discharging), kEps);
    EXPECT_EQ(*st->single().value().text, "Discharging");
    EXPECT_NEAR(lvl->single().value().number, 40.0, kEps);

    p.sample();  // 第 1 份：充電
    EXPECT_NEAR(st->single().value().number, charge_state_code(ChargeState::Charging), kEps);
    EXPECT_EQ(*st->single().value().text, "Charging");
    EXPECT_NEAR(lvl->single().value().number, 45.0, kEps);

    p.sample();  // 第 2 份：滿
    EXPECT_NEAR(st->single().value().number, charge_state_code(ChargeState::Full), kEps);
    EXPECT_EQ(*st->single().value().text, "Full");
    EXPECT_NEAR(lvl->single().value().number, 100.0, kEps);
}

// ===========================================================================
// 是否接電源
// ===========================================================================
TEST(PowerProvider, AcOnlineReflectsPluggedState) {
    {
        auto src = makeFixed(PowerStatus::battery(70.0, ChargeState::Charging, /*on_ac=*/true));
        PowerProvider p{src};
        MetricRegistry reg;
        reg.add_provider(p);
        const auto& v = reg.get("power.ac.online")->single().value();
        ASSERT_TRUE(v.valid);
        EXPECT_NEAR(v.number, 1.0, kEps);
        EXPECT_EQ(*v.text, "Online");
    }
    {
        auto src = makeFixed(PowerStatus::battery(70.0, ChargeState::Discharging, /*on_ac=*/false));
        PowerProvider p{src};
        MetricRegistry reg;
        reg.add_provider(p);
        const auto& v = reg.get("power.ac.online")->single().value();
        ASSERT_TRUE(v.valid);
        EXPECT_NEAR(v.number, 0.0, kEps);
        EXPECT_EQ(*v.text, "Offline");
    }
}

// AC 指標值域有界 [0,1]。
TEST(PowerProvider, AcRangeBoundedZeroToOne) {
    PowerProvider p{std::make_shared<NullPowerStatSource>()};
    MetricRegistry reg;
    reg.add_provider(p);
    auto r = reg.get("power.ac.online")->range();
    ASSERT_TRUE(r.is_bounded());
    EXPECT_NEAR(*r.min, 0.0, kEps);
    EXPECT_NEAR(*r.max, 1.0, kEps);
}

// ===========================================================================
// 剩餘時間估計
// ===========================================================================
TEST(PowerProvider, TimeRemainingMinutes) {
    auto src = makeFixed(PowerStatus::battery(55.0, ChargeState::Discharging, false, 145));
    PowerProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    auto t = reg.get("power.battery.time_remaining");
    ASSERT_NE(t, nullptr);
    const auto& v = t->single().value();
    ASSERT_TRUE(v.valid);
    EXPECT_NEAR(v.number, 145.0, kEps);
    EXPECT_EQ(t->unit(), "min");
    // 值域下界 0、上無界。
    auto r = t->range();
    EXPECT_TRUE(r.has_min());
    EXPECT_FALSE(r.has_max());
    EXPECT_NEAR(*r.min, 0.0, kEps);
}

// 剩餘時間未知（如充電中未估計）→ 未知，不謊報 0。
TEST(PowerProvider, TimeRemainingUnknownWhenAbsent) {
    // battery() 未帶 minutes → nullopt。
    auto src = makeFixed(PowerStatus::battery(90.0, ChargeState::Charging, true));
    PowerProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_FALSE(reg.get("power.battery.time_remaining")->single().value().valid);
    // 但電量 / 狀態 / AC 仍有讀值。
    EXPECT_TRUE(reg.get("power.battery.level")->single().value().valid);
    EXPECT_TRUE(reg.get("power.battery.state")->single().value().valid);
    EXPECT_TRUE(reg.get("power.ac.online")->single().value().valid);
}

// ===========================================================================
// 循環次數 / 健康度（可選）
// ===========================================================================
TEST(PowerProvider, CyclesAndHealthWhenProvided) {
    PowerStatus s = PowerStatus::battery(80.0, ChargeState::Discharging, false, 100);
    s.cycle_count = 342;
    s.health_percent = 91.5;
    PowerProvider p{makeFixed(s)};
    MetricRegistry reg;
    reg.add_provider(p);

    const auto& cyc = reg.get("power.battery.cycles")->single().value();
    ASSERT_TRUE(cyc.valid);
    EXPECT_NEAR(cyc.number, 342.0, kEps);

    const auto& hp = reg.get("power.battery.health")->single().value();
    ASSERT_TRUE(hp.valid);
    EXPECT_NEAR(hp.number, 91.5, kEps);
    EXPECT_EQ(reg.get("power.battery.health")->unit(), "%");
}

// 未提供循環次數 / 健康度 → 該指標未知（可選欄位誠實表達缺讀值）。
TEST(PowerProvider, CyclesAndHealthUnknownWhenAbsent) {
    auto src = makeFixed(PowerStatus::battery(80.0, ChargeState::Discharging, false, 100));
    PowerProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_FALSE(reg.get("power.battery.cycles")->single().value().valid);
    EXPECT_FALSE(reg.get("power.battery.health")->single().value().valid);
}

// ===========================================================================
// 無電池（桌機）情境
// ===========================================================================
TEST(PowerProvider, NoBatteryDesktopScenario) {
    // 桌機：成功讀到電源系統、接 AC、無電池 → 狀態 "no-battery"、AC online，
    // 但電量 / 剩餘時間 / 循環 / 健康度皆未知（不謊報）。
    auto src = makeFixed(PowerStatus::no_battery(/*on_ac=*/true));
    PowerProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    // 狀態誠實為 no-battery（有讀值）。
    const auto& st = reg.get("power.battery.state")->single().value();
    ASSERT_TRUE(st.valid);
    EXPECT_NEAR(st.number, charge_state_code(ChargeState::NoBattery), kEps);
    EXPECT_EQ(*st.text, "No Battery");

    // 接電源有讀值（online）。
    const auto& ac = reg.get("power.ac.online")->single().value();
    ASSERT_TRUE(ac.valid);
    EXPECT_NEAR(ac.number, 1.0, kEps);

    // 電池專屬維度皆未知。
    EXPECT_FALSE(reg.get("power.battery.level")->single().value().valid);
    EXPECT_FALSE(reg.get("power.battery.time_remaining")->single().value().valid);
    EXPECT_FALSE(reg.get("power.battery.cycles")->single().value().valid);
    EXPECT_FALSE(reg.get("power.battery.health")->single().value().valid);
}

// ===========================================================================
// 無讀值 invalid（尚未取樣 / 感測失敗）
// ===========================================================================
TEST(PowerProvider, NoReadingMarksAllMetricsUnknown) {
    // 預設 NullPowerStatSource（未注入）→ 無讀值：全部指標未知（含狀態 / AC）。
    PowerProvider p{std::make_shared<NullPowerStatSource>()};
    MetricRegistry reg;
    reg.add_provider(p);
    for (const char* id : {"power.battery.level", "power.battery.state", "power.ac.online",
                           "power.battery.time_remaining", "power.battery.cycles",
                           "power.battery.health"}) {
        auto m = reg.get(id);
        ASSERT_NE(m, nullptr);
        EXPECT_FALSE(m->single().value().valid);
    }
}

// ===========================================================================
// null 來源行為
// ===========================================================================
TEST(PowerProvider, NullSourceIsConservative) {
    // source 為 null 指標：仍掛上全部指標、各未知、不崩、sample() no-op。
    PowerProvider p{nullptr};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 6u);
    EXPECT_FALSE(reg.get("power.battery.state")->single().value().valid);
    p.sample();  // 不崩
    EXPECT_FALSE(reg.get("power.ac.online")->single().value().valid);
}

// 先無讀值、之後注入 → sample() 後可讀。
TEST(PowerProvider, InjectAfterRegisterBecomesReadable) {
    auto src = std::make_shared<NullPowerStatSource>();
    PowerProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_FALSE(reg.get("power.battery.level")->single().value().valid);

    src->set_status(PowerStatus::battery(77.0, ChargeState::Charging, true, 30));
    p.sample();
    EXPECT_TRUE(reg.get("power.battery.level")->single().value().valid);
    EXPECT_NEAR(reg.get("power.battery.level")->single().value().number, 77.0, kEps);
    EXPECT_NEAR(reg.get("power.battery.time_remaining")->single().value().number, 30.0, kEps);

    // clear 回到無讀值。
    src->clear();
    p.sample();
    EXPECT_FALSE(reg.get("power.battery.level")->single().value().valid);
}

// sample() 未 register_metrics 時為 no-op（不崩）。
TEST(PowerProvider, SampleBeforeRegisterIsNoop) {
    PowerProvider p{makeFixed(PowerStatus::battery(50.0, ChargeState::Full, true))};
    p.sample();  // 尚未 register → no-op，不崩
    SUCCEED();
}

// ===========================================================================
// 序列來源列盡 / 空列語意
// ===========================================================================
TEST(SequencedPowerStatSource, ExhaustionReturnsLastEmptyReturnsUnknown) {
    SequencedPowerStatSource src{std::vector<PowerStatus>{
        PowerStatus::battery(30.0, ChargeState::Discharging, false),
    }};
    EXPECT_NEAR(*src.sample().percent, 30.0, kEps);
    // 列盡 → 持續回最後一份。
    PowerStatus again = src.sample();
    ASSERT_TRUE(again.percent.has_value());
    EXPECT_NEAR(*again.percent, 30.0, kEps);

    // reset 後回起點。
    src.reset();
    EXPECT_NEAR(*src.sample().percent, 30.0, kEps);

    // 空列 → 無讀值。
    SequencedPowerStatSource empty;
    EXPECT_FALSE(empty.sample().valid);
    EXPECT_TRUE(empty.empty());
}

// ===========================================================================
// 經 E2-02 頻率採樣（除頻排程 + 歷史累積）
// ===========================================================================
TEST(PowerProvider, SampledViaE2_02Scheduler) {
    // 一列電量遞降序列（放電）。
    std::vector<PowerStatus> seq;
    for (int k = 0; k < 10; ++k) {
        seq.push_back(PowerStatus::battery(90.0 - k, ChargeState::Discharging, false, 100 - k));
    }
    auto src = std::make_shared<SequencedPowerStatSource>(std::move(seq));
    PowerProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // register 消耗第 0 份（90%）

    SamplingScheduler sched;  // 預設 policy：Low 間隔 = 64 tick
    sched.add_demand(PowerProvider::kLevelId, p.sampling_tier());
    EXPECT_EQ(p.sampling_tier(), SamplingTier::Low);
    ASSERT_TRUE(sched.effective_tier(PowerProvider::kLevelId).has_value());
    EXPECT_EQ(*sched.effective_tier(PowerProvider::kLevelId), SamplingTier::Low);

    // 推進足夠 tick，讓 Low（間隔 64）到期數次，於到期時呼叫 provider.sample()。
    int sampled = 0;
    for (ds::metrics::Tick t = 64; t <= 64 * 3; t += 64) {
        auto due = sched.advance(t);
        for (const auto& id : due) {
            if (id == PowerProvider::kLevelId) {
                p.sample();
                ++sampled;
            }
        }
    }
    EXPECT_EQ(sampled, 3);

    auto lvl = reg.get("power.battery.level");
    const auto& hist = lvl->single().history();
    // register 初值(90) + 3 次採樣(89,88,87) = 4 筆歷史。
    EXPECT_EQ(hist.size(), 4u);
    EXPECT_NEAR(hist.at(0), 90.0, kEps);  // 最舊 = register 初值
    EXPECT_NEAR(hist.latest(), 87.0, kEps);
}

// 除頻：多消費者同一指標合併，最高頻者供給。
TEST(PowerProvider, DeFrequencyCoalescesDemands) {
    PowerProvider p{makeFixed(PowerStatus::battery(50.0, ChargeState::Discharging, false))};
    SamplingScheduler sched;
    auto d_low = sched.add_demand(PowerProvider::kLevelId, SamplingTier::Low);
    sched.add_demand(PowerProvider::kLevelId, SamplingTier::High);
    ASSERT_TRUE(sched.effective_tier(PowerProvider::kLevelId).has_value());
    EXPECT_EQ(*sched.effective_tier(PowerProvider::kLevelId), SamplingTier::High);
    EXPECT_EQ(sched.demand_count(PowerProvider::kLevelId), 2u);

    EXPECT_TRUE(sched.remove_demand(d_low));
    EXPECT_TRUE(sched.tracks(PowerProvider::kLevelId));
    EXPECT_EQ(*sched.effective_tier(PowerProvider::kLevelId), SamplingTier::High);
}

// ===========================================================================
// 範圍 / 消費者範式 / 重複註冊
// ===========================================================================
TEST(PowerProvider, LevelRangeBoundedZeroToHundredNormalizes) {
    PowerProvider p{makeFixed(PowerStatus::battery(50.0, ChargeState::Discharging, false))};
    MetricRegistry reg;
    reg.add_provider(p);
    auto r = reg.get("power.battery.level")->range();
    ASSERT_TRUE(r.is_bounded());
    EXPECT_NEAR(*r.min, 0.0, kEps);
    EXPECT_NEAR(*r.max, 100.0, kEps);
    auto norm = r.normalized(50.0);
    ASSERT_TRUE(norm.has_value());
    EXPECT_NEAR(*norm, 0.5, kEps);
}

// 掛件風格消費者：只透過 E2-01 registry / Metric 抽象介面走訪，完全不觸及具體型別。
TEST(PowerProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = makeFixed(PowerStatus::battery(42.0, ChargeState::Discharging, false, 88));
    PowerProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    // 消費者只認識 MetricRegistry + Metric + MetricInstance。
    std::shared_ptr<Metric> lvl = reg.get("power.battery.level");
    ASSERT_NE(lvl, nullptr);
    EXPECT_EQ(lvl->name(), "Battery Level");
    EXPECT_EQ(lvl->unit(), "%");
    const auto& inst = lvl->single();
    ASSERT_TRUE(inst.value().valid);
    EXPECT_NEAR(inst.value().number, 42.0, kEps);

    // 剩餘時間指標亦只透過抽象讀。
    std::shared_ptr<Metric> t = reg.get("power.battery.time_remaining");
    ASSERT_NE(t, nullptr);
    EXPECT_NEAR(t->single().value().number, 88.0, kEps);
}

TEST(PowerProvider, DuplicateRegistrationRejected) {
    PowerProvider p1{makeFixed(PowerStatus::battery(50.0, ChargeState::Discharging, false))};
    PowerProvider p2{makeFixed(PowerStatus::battery(90.0, ChargeState::Charging, true))};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 6u);
    // 第二個提供者掛同一組 id → 全數保守拒絕（不覆寫既有）。
    EXPECT_EQ(reg.add_provider(p2), 0u);
    EXPECT_EQ(reg.size(), 6u);
}

// ===========================================================================
// 值模型單元
// ===========================================================================
TEST(PowerStatusModel, UnknownAndFactories) {
    PowerStatus u = PowerStatus::unknown();
    EXPECT_FALSE(u.valid);

    PowerStatus nb = PowerStatus::no_battery();
    EXPECT_TRUE(nb.valid);
    EXPECT_EQ(nb.state, ChargeState::NoBattery);
    EXPECT_TRUE(nb.on_ac_power);
    EXPECT_FALSE(nb.percent.has_value());

    PowerStatus b = PowerStatus::battery(60.0, ChargeState::Charging, true, 45);
    EXPECT_TRUE(b.valid);
    ASSERT_TRUE(b.percent.has_value());
    EXPECT_NEAR(*b.percent, 60.0, kEps);
    ASSERT_TRUE(b.minutes_remaining.has_value());
    EXPECT_EQ(*b.minutes_remaining, 45);
    EXPECT_NE(b, u);
    EXPECT_EQ(b, b);
}
