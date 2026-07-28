// E2-17 主機板感測器（溫度 / 風扇 / 電壓）— 測試（gtest）
//
// 覆蓋：提供者身分、註冊到 E2-01 registry（三個指標）、每感測器實例列舉（附名稱 / 類型）、
// 溫度 / 風扇 / 電壓各類讀值正確、多感測器、各類數量獨立變動（增 / 減）、無讀值誠實 invalid、
// 經 E2-02 採樣（除頻排程）、null 來源（固定 / 序列 / 空）、範圍（temp/fan at_least(0)、
// voltage unbounded 支援負軌）、未命名感測器補位 label、消費者只走 E2-01 抽象介面、
// 重複註冊保守拒絕、類型輔助自由函式、值模型單元。
// 相位 1：只驗介面 + 注入式來源行為，不含任何平台分支。
#include "board_sensors.hpp"

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
using ds::sysinfo::BoardSensorSample;
using ds::sysinfo::BoardSensorSource;
using ds::sysinfo::BoardSensorsProvider;
using ds::sysinfo::NullBoardSensorSource;
using ds::sysinfo::SensorReading;
using ds::sysinfo::SensorType;
using ds::sysinfo::to_string;
using ds::sysinfo::unit_for;

namespace {

constexpr double kEps = 1e-9;

// 一份快照：分別給溫度 / 風扇 / 電壓三列讀值。
BoardSensorSample makeSample(std::vector<SensorReading> temps, std::vector<SensorReading> fans,
                             std::vector<SensorReading> volts) {
    BoardSensorSample s;
    s.temperatures = std::move(temps);
    s.fans = std::move(fans);
    s.voltages = std::move(volts);
    return s;
}

// 固定來源：以一份快照建 NullBoardSensorSource。
std::shared_ptr<NullBoardSensorSource> makeSource(BoardSensorSample s) {
    return std::make_shared<NullBoardSensorSource>(std::move(s));
}

}  // namespace

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(BoardSensorsProvider, ProviderIdAndMetricIdsStable) {
    BoardSensorsProvider p{std::make_shared<NullBoardSensorSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.board");
    EXPECT_EQ(std::string(BoardSensorsProvider::kTempId), "board.temp");
    EXPECT_EQ(std::string(BoardSensorsProvider::kFanId), "board.fan");
    EXPECT_EQ(std::string(BoardSensorsProvider::kVoltageId), "board.voltage");
    EXPECT_EQ(std::string(BoardSensorsProvider::kTempName), "Board Temperature");
    EXPECT_EQ(std::string(BoardSensorsProvider::kFanName), "Board Fan Speed");
    EXPECT_EQ(std::string(BoardSensorsProvider::kVoltageName), "Board Voltage");
    EXPECT_EQ(std::string(BoardSensorsProvider::kTempUnit), "\xC2\xB0" "C");  // "°C"
    EXPECT_EQ(std::string(BoardSensorsProvider::kFanUnit), "RPM");
    EXPECT_EQ(std::string(BoardSensorsProvider::kVoltageUnit), "V");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(BoardSensorsProvider, IsMetricProvider) {
    BoardSensorsProvider p{std::make_shared<NullBoardSensorSource>()};
    MetricProvider& mp = p;  // 可上轉
    EXPECT_EQ(mp.provider_id(), "sysinfo.board");
}

// 預設採集分級 = Low（主機板感測器變動慢），可由建構子覆寫。
TEST(BoardSensorsProvider, SamplingTierDefaultsLowOverridable) {
    BoardSensorsProvider def{std::make_shared<NullBoardSensorSource>()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::Low);

    BoardSensorsProvider norm{std::make_shared<NullBoardSensorSource>(),
                              BoardSensorsProvider::kDefaultHistory, SamplingTier::Normal};
    EXPECT_EQ(norm.sampling_tier(), SamplingTier::Normal);
}

// ===========================================================================
// 註冊 / 列舉：三個指標、各類每感測器實例（附名稱）
// ===========================================================================
TEST(BoardSensorsProvider, RegistersThreeMetrics) {
    auto src = makeSource(makeSample(
        {SensorReading::of("CPU", 55.0)},
        {SensorReading::of("CPU Fan", 1200.0)},
        {SensorReading::of("+12V", 12.1)}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);

    EXPECT_EQ(added, 3u);
    EXPECT_TRUE(reg.contains("board.temp"));
    EXPECT_TRUE(reg.contains("board.fan"));
    EXPECT_TRUE(reg.contains("board.voltage"));
    EXPECT_EQ(p.temperature_count(), 1u);
    EXPECT_EQ(p.fan_count(), 1u);
    EXPECT_EQ(p.voltage_count(), 1u);
    EXPECT_EQ(p.sensor_count(), 3u);

    // 每類指標各有 1 個實例，id 前綴依類型、label 取感測器名稱。
    auto temp = reg.get("board.temp");
    ASSERT_EQ(temp->instance_count(), 1u);
    EXPECT_EQ(temp->instance(0).instance_id(), "temp0");
    EXPECT_EQ(temp->instance(0).label(), "CPU");

    auto fan = reg.get("board.fan");
    EXPECT_EQ(fan->instance(0).instance_id(), "fan0");
    EXPECT_EQ(fan->instance(0).label(), "CPU Fan");

    auto volt = reg.get("board.voltage");
    EXPECT_EQ(volt->instance(0).instance_id(), "volt0");
    EXPECT_EQ(volt->instance(0).label(), "+12V");
}

// 溫度 / 風扇 / 電壓各類讀值正確。
TEST(BoardSensorsProvider, TempFanVoltageValues) {
    auto src = makeSource(makeSample(
        {SensorReading::of("CPU", 62.5)},
        {SensorReading::of("CPU Fan", 1450.0)},
        {SensorReading::of("Vcore", 1.25)}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    const auto* t = reg.get("board.temp")->find_instance("temp0");
    ASSERT_NE(t, nullptr);
    ASSERT_TRUE(t->value().valid);
    EXPECT_NEAR(t->value().number, 62.5, kEps);

    const auto* f = reg.get("board.fan")->find_instance("fan0");
    ASSERT_NE(f, nullptr);
    ASSERT_TRUE(f->value().valid);
    EXPECT_NEAR(f->value().number, 1450.0, kEps);

    const auto* v = reg.get("board.voltage")->find_instance("volt0");
    ASSERT_NE(v, nullptr);
    ASSERT_TRUE(v->value().valid);
    EXPECT_NEAR(v->value().number, 1.25, kEps);
}

// ===========================================================================
// 多感測器：每類多顆，各自一組實例
// ===========================================================================
TEST(BoardSensorsProvider, MultiSensorEnumeratesPerType) {
    auto src = makeSource(makeSample(
        {SensorReading::of("CPU", 60.0), SensorReading::of("System", 40.0),
         SensorReading::of("VRM", 70.0)},
        {SensorReading::of("CPU Fan", 1200.0), SensorReading::of("Sys Fan", 800.0)},
        {SensorReading::of("+12V", 12.0), SensorReading::of("+5V", 5.0),
         SensorReading::of("+3.3V", 3.3)}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.temperature_count(), 3u);
    EXPECT_EQ(p.fan_count(), 2u);
    EXPECT_EQ(p.voltage_count(), 3u);

    auto temp = reg.get("board.temp");
    ASSERT_EQ(temp->instance_count(), 3u);
    EXPECT_EQ(temp->instance(2).instance_id(), "temp2");
    EXPECT_EQ(temp->instance(2).label(), "VRM");
    EXPECT_NEAR(temp->find_instance("temp0")->value().number, 60.0, kEps);
    EXPECT_NEAR(temp->find_instance("temp1")->value().number, 40.0, kEps);
    EXPECT_NEAR(temp->find_instance("temp2")->value().number, 70.0, kEps);

    auto volt = reg.get("board.voltage");
    EXPECT_NEAR(volt->find_instance("volt0")->value().number, 12.0, kEps);
    EXPECT_NEAR(volt->find_instance("volt2")->value().number, 3.3, kEps);
}

// ===========================================================================
// 未命名感測器：以類型前綴 + 序號補位 label
// ===========================================================================
TEST(BoardSensorsProvider, UnnamedSensorsGetDefaultLabel) {
    // 名稱留空 → label 補位 "Temp 0" / "Fan 0" / "Voltage 0"。
    auto src = makeSource(makeSample(
        {SensorReading::of("", 50.0)},
        {SensorReading::of("", 1000.0)},
        {SensorReading::of("", 3.3)}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(reg.get("board.temp")->instance(0).label(), "Temp 0");
    EXPECT_EQ(reg.get("board.fan")->instance(0).label(), "Fan 0");
    EXPECT_EQ(reg.get("board.voltage")->instance(0).label(), "Voltage 0");
    // id 前綴仍固定。
    EXPECT_EQ(reg.get("board.temp")->instance(0).instance_id(), "temp0");
}

// ===========================================================================
// 各類數量獨立變動：增 / 減
// ===========================================================================
TEST(BoardSensorsProvider, SensorCountIncreaseAddsInstances) {
    auto src = makeSource(makeSample(
        {SensorReading::of("CPU", 55.0)}, {SensorReading::of("CPU Fan", 1000.0)}, {}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.temperature_count(), 1u);
    EXPECT_EQ(p.voltage_count(), 0u);
    auto temp = reg.get("board.temp");
    const auto* t0_before = temp->find_instance("temp0");

    // 溫度增為 2 顆、電壓增為 1 顆。
    src->set_fixed(makeSample(
        {SensorReading::of("CPU", 55.0), SensorReading::of("System", 42.0)},
        {SensorReading::of("CPU Fan", 1000.0)},
        {SensorReading::of("+12V", 12.0)}));
    p.sample();
    EXPECT_EQ(p.temperature_count(), 2u);
    EXPECT_EQ(p.voltage_count(), 1u);
    // 既有 temp0 參照仍有效（unique_ptr 持有實例）。
    EXPECT_EQ(temp->find_instance("temp0"), t0_before);
    EXPECT_NE(temp->find_instance("temp1"), nullptr);
    EXPECT_EQ(temp->find_instance("temp1")->label(), "System");
    EXPECT_NEAR(temp->find_instance("temp1")->value().number, 42.0, kEps);
    EXPECT_NEAR(reg.get("board.voltage")->find_instance("volt0")->value().number, 12.0, kEps);
}

TEST(BoardSensorsProvider, SensorCountDecreaseMarksMissingUnknown) {
    auto src = makeSource(makeSample(
        {SensorReading::of("CPU", 55.0), SensorReading::of("System", 42.0)},
        {SensorReading::of("CPU Fan", 1000.0)}, {}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.temperature_count(), 2u);

    // 溫度降為 1 顆。
    src->set_fixed(makeSample({SensorReading::of("CPU", 55.0)},
                              {SensorReading::of("CPU Fan", 1000.0)}, {}));
    p.sample();
    // 實例數不縮減（2 顆仍在）。
    EXPECT_EQ(p.temperature_count(), 2u);
    auto temp = reg.get("board.temp");
    EXPECT_TRUE(temp->find_instance("temp0")->value().valid);
    // temp1 下線 → 未知。
    EXPECT_FALSE(temp->find_instance("temp1")->value().valid);
}

// ===========================================================================
// 無讀值誠實 invalid（不謊報 0）
// ===========================================================================
TEST(BoardSensorsProvider, InvalidReadingIsHonest) {
    // 一顆溫度有讀、一顆溫度無讀；風扇無讀。
    auto src = makeSource(makeSample(
        {SensorReading::of("CPU", 60.0), SensorReading::unknown("System")},
        {SensorReading::unknown("CPU Fan")},
        {SensorReading::of("+12V", 12.0)}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    EXPECT_TRUE(reg.get("board.temp")->find_instance("temp0")->value().valid);
    EXPECT_NEAR(reg.get("board.temp")->find_instance("temp0")->value().number, 60.0, kEps);
    // 無讀值 → 未知，而非 0。
    EXPECT_FALSE(reg.get("board.temp")->find_instance("temp1")->value().valid);
    EXPECT_FALSE(reg.get("board.fan")->find_instance("fan0")->value().valid);
    // 具名但無讀值：label 仍在（感測器存在）。
    EXPECT_EQ(reg.get("board.temp")->find_instance("temp1")->label(), "System");
}

// 無讀值不推入歷史（不污染序列）。
TEST(BoardSensorsProvider, UnknownNotPushedToHistory) {
    auto src = makeSource(makeSample({SensorReading::unknown("CPU")}, {}, {}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    p.sample();
    p.sample();
    // 三次未知（register + 2×sample）→ 歷史仍空。
    EXPECT_TRUE(reg.get("board.temp")->find_instance("temp0")->history().empty());
}

// 有效讀值累積歷史。
TEST(BoardSensorsProvider, ValidReadingAccumulatesHistory) {
    auto src = std::make_shared<NullBoardSensorSource>(std::vector<BoardSensorSample>{
        makeSample({SensorReading::of("CPU", 50.0)}, {}, {}),
        makeSample({SensorReading::of("CPU", 55.0)}, {}, {}),
        makeSample({SensorReading::of("CPU", 60.0)}, {}, {}),
    });
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // 第 0 份 → 50
    p.sample();           // 第 1 份 → 55
    p.sample();           // 第 2 份 → 60
    const auto& hist = reg.get("board.temp")->find_instance("temp0")->history();
    ASSERT_EQ(hist.size(), 3u);
    EXPECT_NEAR(hist.at(0), 50.0, kEps);   // 最舊
    EXPECT_NEAR(hist.latest(), 60.0, kEps);  // 最新
}

// ===========================================================================
// 經 E2-02 頻率採樣（除頻排程）
// ===========================================================================
TEST(BoardSensorsProvider, SampledViaE2_02Scheduler) {
    auto src = makeSource(makeSample({SensorReading::of("CPU", 55.0)}, {}, {}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // register 內首次 sample → 歷史 1 筆

    SamplingScheduler sched;
    sched.add_demand(BoardSensorsProvider::kTempId, p.sampling_tier());  // Low 間隔 64

    // 推進 192 個 tick，Low 間隔 64 → 於 tick 64/128/192 到期採樣 3 次。
    int sampled = 0;
    for (ds::metrics::Tick t = 1; t <= 192; ++t) {
        auto due = sched.advance(t);
        for (const auto& id : due) {
            if (id == BoardSensorsProvider::kTempId) {
                p.sample();
                ++sampled;
            }
        }
    }
    EXPECT_EQ(sampled, 3);
    // register 首採 1 筆 + 排程 3 筆 = 4 筆。
    EXPECT_EQ(reg.get("board.temp")->find_instance("temp0")->history().size(), 4u);
}

// 除頻：多消費者同一指標合併，最高頻者供給。
TEST(BoardSensorsProvider, DeFrequencyCoalescesDemands) {
    SamplingScheduler sched;
    auto d_low = sched.add_demand(BoardSensorsProvider::kTempId, SamplingTier::Low);
    sched.add_demand(BoardSensorsProvider::kTempId, SamplingTier::High);
    ASSERT_TRUE(sched.effective_tier(BoardSensorsProvider::kTempId).has_value());
    EXPECT_EQ(*sched.effective_tier(BoardSensorsProvider::kTempId), SamplingTier::High);
    EXPECT_EQ(sched.demand_count(BoardSensorsProvider::kTempId), 2u);
    EXPECT_TRUE(sched.remove_demand(d_low));
    EXPECT_EQ(*sched.effective_tier(BoardSensorsProvider::kTempId), SamplingTier::High);
}

// ===========================================================================
// null 來源行為（固定 / 序列 / 空）
// ===========================================================================
TEST(BoardSensorsProvider, NullSourceIsConservative) {
    // source 為 null 指標：仍掛上三個指標，各 0 實例、不崩。
    BoardSensorsProvider p{nullptr};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 3u);
    EXPECT_EQ(reg.get("board.temp")->instance_count(), 0u);
    EXPECT_EQ(reg.get("board.fan")->instance_count(), 0u);
    EXPECT_EQ(reg.get("board.voltage")->instance_count(), 0u);
    EXPECT_EQ(p.sensor_count(), 0u);
    p.sample();  // 不崩
    EXPECT_EQ(p.sensor_count(), 0u);
}

TEST(BoardSensorsProvider, NullBoardSensorSourceDefaultEmpty) {
    // NullBoardSensorSource 預設（未注入）→ 空快照、0 感測器。
    auto src = std::make_shared<NullBoardSensorSource>();
    EXPECT_TRUE(src->empty());
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.sensor_count(), 0u);

    // 之後注入資料 → sample() 後可讀。
    src->set_fixed(makeSample({SensorReading::of("CPU", 66.0)}, {}, {}));
    p.sample();
    EXPECT_EQ(p.temperature_count(), 1u);
    EXPECT_NEAR(reg.get("board.temp")->find_instance("temp0")->value().number, 66.0, kEps);
}

// 序列來源：每次 sample() 回下一份；列盡回最後一份。
TEST(NullBoardSensorSource, SequenceThenExhaustion) {
    NullBoardSensorSource src{std::vector<BoardSensorSample>{
        makeSample({SensorReading::of("CPU", 50.0)}, {}, {}),
        makeSample({SensorReading::of("CPU", 55.0)}, {}, {}),
    }};
    EXPECT_EQ(src.size(), 2u);
    EXPECT_NEAR(src.sample().temperatures[0].value, 50.0, kEps);
    EXPECT_NEAR(src.sample().temperatures[0].value, 55.0, kEps);
    // 列盡 → 持續回最後一份。
    EXPECT_NEAR(src.sample().temperatures[0].value, 55.0, kEps);
}

// 固定來源：每次 sample() 皆回同一份；clear → 空。
TEST(NullBoardSensorSource, FixedReturnsSameThenClear) {
    NullBoardSensorSource src{makeSample({SensorReading::of("CPU", 42.0)}, {}, {})};
    EXPECT_NEAR(src.sample().temperatures[0].value, 42.0, kEps);
    EXPECT_NEAR(src.sample().temperatures[0].value, 42.0, kEps);
    src.clear();
    EXPECT_TRUE(src.sample().empty());
}

// reset 游標回起點。
TEST(NullBoardSensorSource, ResetRewindsCursor) {
    NullBoardSensorSource src{std::vector<BoardSensorSample>{
        makeSample({SensorReading::of("CPU", 50.0)}, {}, {}),
        makeSample({SensorReading::of("CPU", 55.0)}, {}, {}),
    }};
    EXPECT_NEAR(src.sample().temperatures[0].value, 50.0, kEps);
    EXPECT_NEAR(src.sample().temperatures[0].value, 55.0, kEps);
    src.reset();
    EXPECT_NEAR(src.sample().temperatures[0].value, 50.0, kEps);  // 回起點
}

// sample() 未 register_metrics 時為 no-op（不崩）。
TEST(BoardSensorsProvider, SampleBeforeRegisterIsNoop) {
    BoardSensorsProvider p{makeSource(makeSample({SensorReading::of("CPU", 55.0)}, {}, {}))};
    p.sample();  // 尚未 register → no-op，不崩
    EXPECT_EQ(p.sensor_count(), 0u);
}

// ===========================================================================
// 範圍 / 消費者範式 / 重複註冊
// ===========================================================================
TEST(BoardSensorsProvider, RangesTempFanLowerBoundedVoltageUnbounded) {
    auto src = makeSource(makeSample(
        {SensorReading::of("CPU", 55.0)}, {SensorReading::of("CPU Fan", 1000.0)},
        {SensorReading::of("+12V", 12.0)}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    // 溫度：下界 0、無上界。
    auto tr = reg.get("board.temp")->range();
    EXPECT_TRUE(tr.has_min());
    EXPECT_FALSE(tr.has_max());
    EXPECT_NEAR(*tr.min, 0.0, kEps);
    // 風扇：下界 0、無上界。
    auto fr = reg.get("board.fan")->range();
    EXPECT_TRUE(fr.has_min());
    EXPECT_FALSE(fr.has_max());
    // 電壓：上下皆無界（支援負軌）。
    auto vr = reg.get("board.voltage")->range();
    EXPECT_FALSE(vr.has_min());
    EXPECT_FALSE(vr.has_max());
    EXPECT_FALSE(vr.is_bounded());
}

// 電壓負軌（如 -12V）：unbounded range 誠實承載負值。
TEST(BoardSensorsProvider, NegativeVoltageRailSupported) {
    auto src = makeSource(makeSample({}, {}, {SensorReading::of("-12V", -12.05)}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    const auto* v = reg.get("board.voltage")->find_instance("volt0");
    ASSERT_NE(v, nullptr);
    ASSERT_TRUE(v->value().valid);
    EXPECT_NEAR(v->value().number, -12.05, kEps);
    EXPECT_EQ(v->label(), "-12V");
}

// 掛件風格消費者：只透過 E2-01 registry / Metric 抽象介面走訪，完全不觸及具體型別。
TEST(BoardSensorsProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = makeSource(makeSample(
        {SensorReading::of("CPU", 60.0), SensorReading::of("System", 40.0)}, {}, {}));
    BoardSensorsProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    std::shared_ptr<Metric> m = reg.get("board.temp");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), "\xC2\xB0" "C");  // "°C"
    double sum = 0.0;
    for (std::size_t i = 0; i < m->instance_count(); ++i) {
        const auto& inst = m->instance(i);
        if (inst.value().valid) sum += inst.value().number;
    }
    EXPECT_NEAR(sum, 100.0, kEps);  // 60 + 40
}

TEST(BoardSensorsProvider, DuplicateRegistrationRejected) {
    BoardSensorsProvider p1{makeSource(makeSample({SensorReading::of("CPU", 55.0)}, {}, {}))};
    BoardSensorsProvider p2{makeSource(makeSample({SensorReading::of("CPU", 80.0)}, {}, {}))};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 3u);
    // 第二個提供者掛同一組 id → 三個皆保守拒絕（不覆寫既有）。
    EXPECT_EQ(reg.add_provider(p2), 0u);
    EXPECT_EQ(reg.size(), 3u);
}

// ===========================================================================
// 類型輔助自由函式 / 值模型單元
// ===========================================================================
TEST(SensorType, UnitAndToString) {
    EXPECT_EQ(std::string(unit_for(SensorType::Temperature)), "\xC2\xB0" "C");  // "°C"
    EXPECT_EQ(std::string(unit_for(SensorType::Fan)), "RPM");
    EXPECT_EQ(std::string(unit_for(SensorType::Voltage)), "V");
    EXPECT_EQ(std::string(to_string(SensorType::Temperature)), "temperature");
    EXPECT_EQ(std::string(to_string(SensorType::Fan)), "fan");
    EXPECT_EQ(std::string(to_string(SensorType::Voltage)), "voltage");
}

TEST(SensorReading, FactoriesAndEquality) {
    SensorReading u = SensorReading::unknown("System");
    EXPECT_FALSE(u.valid);
    EXPECT_EQ(u.name, "System");

    SensorReading g = SensorReading::of("CPU", 55.0);
    EXPECT_TRUE(g.valid);
    EXPECT_EQ(g.name, "CPU");
    EXPECT_NEAR(g.value, 55.0, kEps);
    EXPECT_NE(g, u);

    SensorReading g2 = SensorReading::of("CPU", 55.0);
    EXPECT_EQ(g, g2);
}

TEST(BoardSensorSample, CountsAndEquality) {
    BoardSensorSample a = makeSample(
        {SensorReading::of("CPU", 55.0)}, {SensorReading::of("CPU Fan", 1000.0)},
        {SensorReading::of("+12V", 12.0)});
    EXPECT_EQ(a.temperature_count(), 1u);
    EXPECT_EQ(a.fan_count(), 1u);
    EXPECT_EQ(a.voltage_count(), 1u);
    EXPECT_EQ(a.total_count(), 3u);
    EXPECT_FALSE(a.empty());

    BoardSensorSample b = a;
    EXPECT_EQ(a, b);
    b.temperatures.push_back(SensorReading::of("System", 40.0));
    EXPECT_NE(a, b);
    EXPECT_EQ(b.total_count(), 4u);

    EXPECT_TRUE(BoardSensorSample{}.empty());
    EXPECT_EQ(BoardSensorSample{}.total_count(), 0u);
}
