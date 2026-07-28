// E2-24 Wi-Fi 狀態 — 測試（gtest）
//
// 覆蓋：提供者身分、註冊到 E2-01 registry、七個面向指標與單一 Wi-Fi 實例列舉、
// SSID / 訊號(RSSI+%) / 速率 / 頻道(+頻段) / 安全類型 / 是否已連線各欄位讀值、
// 已連線 vs 未連線、無讀值（source null / unknown）誠實 invalid、訊號強度邊界
// （rssi_to_percent 夾 [0,100]）、經 E2-02 採樣（除頻排程）鋪歷史、null / 序列來源、
// 範圍設定、消費者只走 E2-01 抽象介面、重複註冊保守拒絕。
// 相位 1：只驗介面 + 注入式來源行為，不含任何平台分支 / 真實 Wi-Fi API。
#include "wifi_status.hpp"

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
using ds::sysinfo::NullWifiSource;
using ds::sysinfo::rssi_to_percent;
using ds::sysinfo::WifiBand;
using ds::sysinfo::WifiSecurity;
using ds::sysinfo::WifiSource;
using ds::sysinfo::WifiStatus;
using ds::sysinfo::WifiStatusProvider;

namespace {

constexpr double kEps = 1e-9;

// 一份典型「已連線」狀態：SSID "HomeNet"、-60 dBm、300 Mbps、頻道 36、5 GHz、WPA2。
WifiStatus makeConnected() {
    return WifiStatus::connected_to("HomeNet", -60.0, 300.0, 36, WifiBand::Band5GHz,
                                    WifiSecurity::WPA2);
}

// 便利：取某指標的單一 Wi-Fi 實例值。
MetricValue facetValue(const MetricRegistry& reg, const char* id) {
    auto m = reg.get(id);
    return m->single().value();
}

}  // namespace

// ===========================================================================
// 提供者身分 / 契約
// ===========================================================================
TEST(WifiStatusProvider, ProviderIdIsStable) {
    WifiStatusProvider p{std::make_shared<NullWifiSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.wifi");
    EXPECT_EQ(std::string(WifiStatusProvider::kConnectedMetricId), "wifi.connected");
    EXPECT_EQ(std::string(WifiStatusProvider::kSsidMetricId), "wifi.ssid");
    EXPECT_EQ(std::string(WifiStatusProvider::kRssiMetricId), "wifi.rssi");
    EXPECT_EQ(std::string(WifiStatusProvider::kSignalMetricId), "wifi.signal");
    EXPECT_EQ(std::string(WifiStatusProvider::kRateMetricId), "wifi.rate");
    EXPECT_EQ(std::string(WifiStatusProvider::kChannelMetricId), "wifi.channel");
    EXPECT_EQ(std::string(WifiStatusProvider::kSecurityMetricId), "wifi.security");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(WifiStatusProvider, IsMetricProvider) {
    WifiStatusProvider p{std::make_shared<NullWifiSource>()};
    MetricProvider& mp = p;  // 可上轉
    EXPECT_EQ(mp.provider_id(), "sysinfo.wifi");
}

// 預設採集分級 = Normal（連線狀態變動不快），可由建構子覆寫。
TEST(WifiStatusProvider, SamplingTierDefaultsNormalOverridable) {
    WifiStatusProvider def{std::make_shared<NullWifiSource>()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::Normal);

    WifiStatusProvider low{std::make_shared<NullWifiSource>(),
                           WifiStatusProvider::kDefaultHistory, SamplingTier::Low};
    EXPECT_EQ(low.sampling_tier(), SamplingTier::Low);
}

// metric_ids() 回七個面向 id（註冊順序）。
TEST(WifiStatusProvider, MetricIdsListsSevenFacets) {
    auto ids = WifiStatusProvider::metric_ids();
    ASSERT_EQ(ids.size(), 7u);
    EXPECT_EQ(ids[0], "wifi.connected");
    EXPECT_EQ(ids[1], "wifi.ssid");
    EXPECT_EQ(ids[2], "wifi.rssi");
    EXPECT_EQ(ids[3], "wifi.signal");
    EXPECT_EQ(ids[4], "wifi.rate");
    EXPECT_EQ(ids[5], "wifi.channel");
    EXPECT_EQ(ids[6], "wifi.security");
}

// ===========================================================================
// 註冊 / 列舉：七個指標，各一單一 Wi-Fi 實例
// ===========================================================================
TEST(WifiStatusProvider, RegistersSevenMetricsEachSingleInstance) {
    auto src = std::make_shared<NullWifiSource>(makeConnected());
    WifiStatusProvider p{src};
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);

    EXPECT_EQ(added, 7u);
    for (const auto& id : WifiStatusProvider::metric_ids()) {
        auto m = reg.get(id);
        ASSERT_NE(m, nullptr);
        EXPECT_TRUE(m->is_single()) << id;
        EXPECT_EQ(m->instance(0).instance_id(), "wifi0") << id;
        EXPECT_EQ(m->instance(0).label(), "Wi-Fi") << id;
    }
}

// 範圍（range）設定正確：connected bounded[0,1]、rssi bounded[-100,0]、signal bounded[0,100]、
// rate at_least 0、channel at_least 0、ssid / security 無界。
TEST(WifiStatusProvider, MetricRangesAreCorrect) {
    auto src = std::make_shared<NullWifiSource>(makeConnected());
    WifiStatusProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    auto conn = reg.get("wifi.connected");
    EXPECT_TRUE(conn->range().is_bounded());
    EXPECT_DOUBLE_EQ(*conn->range().min, 0.0);
    EXPECT_DOUBLE_EQ(*conn->range().max, 1.0);

    auto rssi = reg.get("wifi.rssi");
    EXPECT_TRUE(rssi->range().is_bounded());
    EXPECT_DOUBLE_EQ(*rssi->range().min, -100.0);
    EXPECT_DOUBLE_EQ(*rssi->range().max, 0.0);
    EXPECT_EQ(rssi->unit(), "dBm");

    auto sig = reg.get("wifi.signal");
    EXPECT_TRUE(sig->range().is_bounded());
    EXPECT_DOUBLE_EQ(*sig->range().min, 0.0);
    EXPECT_DOUBLE_EQ(*sig->range().max, 100.0);
    EXPECT_EQ(sig->unit(), "%");

    auto rate = reg.get("wifi.rate");
    EXPECT_TRUE(rate->range().has_min());
    EXPECT_FALSE(rate->range().has_max());
    EXPECT_EQ(rate->unit(), "Mbps");

    auto chan = reg.get("wifi.channel");
    EXPECT_TRUE(chan->range().has_min());
    EXPECT_FALSE(chan->range().has_max());

    auto ssid = reg.get("wifi.ssid");
    EXPECT_FALSE(ssid->range().has_min());
    EXPECT_FALSE(ssid->range().has_max());

    auto sec = reg.get("wifi.security");
    EXPECT_FALSE(sec->range().is_bounded());
}

// ===========================================================================
// 已連線：各欄位讀值（SSID / RSSI / 訊號% / 速率 / 頻道+頻段 / 安全類型 / 已連線）
// ===========================================================================
TEST(WifiStatusProvider, ConnectedFieldsReadCorrectly) {
    auto src = std::make_shared<NullWifiSource>(makeConnected());
    WifiStatusProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    // 是否已連線：值 = 1，文字 "Connected"。
    const MetricValue conn = facetValue(reg, "wifi.connected");
    EXPECT_TRUE(conn.valid);
    EXPECT_DOUBLE_EQ(conn.number, 1.0);
    ASSERT_TRUE(conn.text.has_value());
    EXPECT_EQ(*conn.text, "Connected");

    // SSID：文字表述 "HomeNet"。
    const MetricValue ssid = facetValue(reg, "wifi.ssid");
    EXPECT_TRUE(ssid.valid);
    ASSERT_TRUE(ssid.text.has_value());
    EXPECT_EQ(*ssid.text, "HomeNet");

    // RSSI：-60 dBm。
    const MetricValue rssi = facetValue(reg, "wifi.rssi");
    EXPECT_TRUE(rssi.valid);
    EXPECT_NEAR(rssi.number, -60.0, kEps);

    // 訊號 %：rssi_to_percent(-60) = 2*(−60+100) = 80%。
    const MetricValue sig = facetValue(reg, "wifi.signal");
    EXPECT_TRUE(sig.valid);
    EXPECT_NEAR(sig.number, 80.0, kEps);

    // 連線速率：300 Mbps。
    const MetricValue rate = facetValue(reg, "wifi.rate");
    EXPECT_TRUE(rate.valid);
    EXPECT_NEAR(rate.number, 300.0, kEps);

    // 頻道：36，文字頻段 "5 GHz"。
    const MetricValue chan = facetValue(reg, "wifi.channel");
    EXPECT_TRUE(chan.valid);
    EXPECT_NEAR(chan.number, 36.0, kEps);
    ASSERT_TRUE(chan.text.has_value());
    EXPECT_EQ(*chan.text, "5 GHz");

    // 安全類型：WPA2（ordinal 4），文字 "wpa2"。
    const MetricValue sec = facetValue(reg, "wifi.security");
    EXPECT_TRUE(sec.valid);
    ASSERT_TRUE(sec.text.has_value());
    EXPECT_EQ(*sec.text, "wpa2");
    EXPECT_NEAR(sec.number, static_cast<double>(static_cast<int>(WifiSecurity::WPA2)), kEps);
}

// ===========================================================================
// 未連線：「已連線」面向有效（值 = 0 / "Disconnected"），連線相依面向誠實未知
// ===========================================================================
TEST(WifiStatusProvider, NotConnectedMarksLinkFacetsUnknown) {
    auto src = std::make_shared<NullWifiSource>(WifiStatus::not_connected());
    WifiStatusProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    // 是否已連線：有讀值，值 = 0、文字 "Disconnected"。
    const MetricValue conn = facetValue(reg, "wifi.connected");
    EXPECT_TRUE(conn.valid);
    EXPECT_DOUBLE_EQ(conn.number, 0.0);
    ASSERT_TRUE(conn.text.has_value());
    EXPECT_EQ(*conn.text, "Disconnected");

    // 連線相依面向：未連線即無讀值 → 誠實 invalid（未知），不謊報 0。
    EXPECT_FALSE(facetValue(reg, "wifi.ssid").valid);
    EXPECT_FALSE(facetValue(reg, "wifi.rssi").valid);
    EXPECT_FALSE(facetValue(reg, "wifi.signal").valid);
    EXPECT_FALSE(facetValue(reg, "wifi.rate").valid);
    EXPECT_FALSE(facetValue(reg, "wifi.channel").valid);
    EXPECT_FALSE(facetValue(reg, "wifi.security").valid);
}

// ===========================================================================
// 無讀值：整體 unknown → 所有面向（含 connected）誠實 invalid
// ===========================================================================
TEST(WifiStatusProvider, NoReadingAllFacetsInvalid) {
    // 空序列 NullWifiSource → 每次 sample() 回 unknown。
    auto src = std::make_shared<NullWifiSource>();
    WifiStatusProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    for (const auto& id : WifiStatusProvider::metric_ids()) {
        EXPECT_FALSE(facetValue(reg, id.c_str()).valid) << id;
    }
}

// source 為 null 指標時：仍掛上七個指標，皆未知（保守不崩、不謊報 0）。
TEST(WifiStatusProvider, NullSourcePointerStillRegistersAllUnknown) {
    WifiStatusProvider p{nullptr};
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);
    EXPECT_EQ(added, 7u);
    for (const auto& id : WifiStatusProvider::metric_ids()) {
        auto m = reg.get(id);
        ASSERT_NE(m, nullptr);
        EXPECT_TRUE(m->is_single());
        EXPECT_FALSE(m->single().value().valid) << id;
    }
}

// ===========================================================================
// 訊號強度邊界：rssi_to_percent 夾到 [0,100]
// ===========================================================================
TEST(WifiStatus, RssiToPercentBoundaries) {
    EXPECT_NEAR(rssi_to_percent(-50.0), 100.0, kEps);   // 極強
    EXPECT_NEAR(rssi_to_percent(-100.0), 0.0, kEps);    // 極弱
    EXPECT_NEAR(rssi_to_percent(-75.0), 50.0, kEps);    // 中點
    // 超界夾住：
    EXPECT_NEAR(rssi_to_percent(-30.0), 100.0, kEps);   // 高於 -50 → 夾 100
    EXPECT_NEAR(rssi_to_percent(0.0), 100.0, kEps);
    EXPECT_NEAR(rssi_to_percent(-120.0), 0.0, kEps);    // 低於 -100 → 夾 0
}

// 已連線工廠：signal_percent 由 rssi_to_percent 換算。
TEST(WifiStatus, ConnectedFactoryDerivesSignalPercent) {
    WifiStatus s = WifiStatus::connected_to("N", -70.0, 100.0, 6, WifiBand::Band2_4GHz,
                                            WifiSecurity::WPA3);
    EXPECT_TRUE(s.valid);
    EXPECT_TRUE(s.connected);
    EXPECT_NEAR(s.signal_percent, rssi_to_percent(-70.0), kEps);  // = 60%
    EXPECT_NEAR(s.signal_percent, 60.0, kEps);
}

// 邊界訊號值可經指標範圍正規化：signal 100% → normalized 1。
TEST(WifiStatusProvider, SignalBoundaryNormalizes) {
    // 極強：-50 dBm → 100%。
    auto strong = std::make_shared<NullWifiSource>(WifiStatus::connected_to(
        "S", -50.0, 866.0, 149, WifiBand::Band5GHz, WifiSecurity::WPA3));
    WifiStatusProvider p{strong};
    MetricRegistry reg;
    reg.add_provider(p);
    auto sig = reg.get("wifi.signal");
    const MetricValue v = sig->single().value();
    EXPECT_NEAR(v.number, 100.0, kEps);
    auto norm = sig->range().normalized(v.number);
    ASSERT_TRUE(norm.has_value());
    EXPECT_NEAR(*norm, 1.0, kEps);
}

// ===========================================================================
// 列舉 → 字串
// ===========================================================================
TEST(WifiStatus, BandAndSecurityToString) {
    EXPECT_EQ(std::string(ds::sysinfo::to_string(WifiBand::Unknown)), "unknown");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(WifiBand::Band2_4GHz)), "2.4 GHz");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(WifiBand::Band5GHz)), "5 GHz");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(WifiBand::Band6GHz)), "6 GHz");

    EXPECT_EQ(std::string(ds::sysinfo::to_string(WifiSecurity::Open)), "open");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(WifiSecurity::WEP)), "wep");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(WifiSecurity::WPA)), "wpa");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(WifiSecurity::WPA2)), "wpa2");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(WifiSecurity::WPA3)), "wpa3");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(WifiSecurity::Enterprise)), "enterprise");
    EXPECT_EQ(std::string(ds::sysinfo::to_string(WifiSecurity::Unknown)), "unknown");
}

// ===========================================================================
// null / 序列來源行為
// ===========================================================================
TEST(NullWifiSource, DefaultsToUnknown) {
    NullWifiSource src;
    EXPECT_TRUE(src.empty());
    EXPECT_FALSE(src.sample().valid);
}

TEST(NullWifiSource, FixedStatusRepeats) {
    NullWifiSource src{makeConnected()};
    EXPECT_EQ(src.size(), 1u);
    EXPECT_EQ(src.sample(), makeConnected());
    EXPECT_EQ(src.sample(), makeConnected());  // 固定重複
}

TEST(NullWifiSource, SequenceAdvancesThenHoldsLast) {
    std::vector<WifiStatus> seq = {
        makeConnected(),
        WifiStatus::not_connected(),
    };
    NullWifiSource src{seq};
    EXPECT_EQ(src.sample(), makeConnected());
    EXPECT_EQ(src.sample(), WifiStatus::not_connected());
    EXPECT_EQ(src.sample(), WifiStatus::not_connected());  // 列盡回最後一份
}

TEST(NullWifiSource, SetStatusAndClear) {
    NullWifiSource src;
    src.set_status(makeConnected());
    EXPECT_TRUE(src.sample().valid);
    src.clear();
    EXPECT_TRUE(src.empty());
    EXPECT_FALSE(src.sample().valid);
}

// 消費者只走 E2-01 抽象介面（WifiSource 可上轉、注入 provider）。
TEST(WifiSourceContract, AbstractSampleWorks) {
    std::shared_ptr<WifiSource> src = std::make_shared<NullWifiSource>(makeConnected());
    EXPECT_TRUE(src->sample().valid);
}

// ===========================================================================
// sample()：狀態變化重新讀取、掉線後連線相依面向轉未知
// ===========================================================================
TEST(WifiStatusProvider, SampleReReadsSourceOnDisconnect) {
    auto src = std::make_shared<NullWifiSource>(std::vector<WifiStatus>{
        makeConnected(),                 // register 取這份
        WifiStatus::not_connected(),     // 下一次 sample 掉線
    });
    WifiStatusProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    // 註冊時已連線：SSID 有效。
    EXPECT_TRUE(facetValue(reg, "wifi.ssid").valid);
    EXPECT_TRUE(facetValue(reg, "wifi.signal").valid);

    // 再採一次 → 掉線：connected 有效值 0、SSID / 訊號轉未知。
    p.sample();
    const MetricValue conn = facetValue(reg, "wifi.connected");
    EXPECT_TRUE(conn.valid);
    EXPECT_DOUBLE_EQ(conn.number, 0.0);
    EXPECT_FALSE(facetValue(reg, "wifi.ssid").valid);
    EXPECT_FALSE(facetValue(reg, "wifi.signal").valid);
}

// register 前 sample() 為 no-op（不崩）。
TEST(WifiStatusProvider, SampleBeforeRegisterIsNoop) {
    WifiStatusProvider p{std::make_shared<NullWifiSource>(makeConnected())};
    p.sample();  // 無指標 → no-op
    SUCCEED();
}

// ===========================================================================
// 歷史：register 推第一份，之後每次採集推一份（數值面向 history = 1 + 採樣次數）
// ===========================================================================
TEST(WifiStatusProvider, NumericFacetsAccumulateHistory) {
    // 序列：三份訊號漸強的已連線狀態。
    auto src = std::make_shared<NullWifiSource>(std::vector<WifiStatus>{
        WifiStatus::connected_to("N", -80.0, 100.0, 1, WifiBand::Band2_4GHz, WifiSecurity::WPA2),
        WifiStatus::connected_to("N", -70.0, 200.0, 1, WifiBand::Band2_4GHz, WifiSecurity::WPA2),
        WifiStatus::connected_to("N", -60.0, 300.0, 1, WifiBand::Band2_4GHz, WifiSecurity::WPA2),
    });
    WifiStatusProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // register 取第 1 份，推入歷史（size = 1）

    p.sample();  // 第 2 份（size = 2）
    p.sample();  // 第 3 份（size = 3）

    auto sig = reg.get("wifi.signal");
    const auto& hist = sig->single().history();
    ASSERT_EQ(hist.size(), 3u);
    EXPECT_NEAR(hist.at(0), rssi_to_percent(-80.0), kEps);  // 最舊 = 40%
    EXPECT_NEAR(hist.at(2), rssi_to_percent(-60.0), kEps);  // 最新 = 80%
    EXPECT_NEAR(hist.latest(), 80.0, kEps);

    // 速率歷史同步鋪成時序。
    auto rate = reg.get("wifi.rate");
    ASSERT_EQ(rate->single().history().size(), 3u);
    EXPECT_NEAR(rate->single().history().latest(), 300.0, kEps);
}

// categorical 面向（ssid / security）不污染歷史（僅設值，不推序列）。
TEST(WifiStatusProvider, CategoricalFacetsDoNotAccumulateHistory) {
    auto src = std::make_shared<NullWifiSource>(makeConnected());
    WifiStatusProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    p.sample();
    p.sample();

    // ssid / security 為 categorical：歷史不累積（設值語意，非時序）。
    EXPECT_EQ(reg.get("wifi.ssid")->single().history().size(), 0u);
    EXPECT_EQ(reg.get("wifi.security")->single().history().size(), 0u);
}

// ===========================================================================
// 經 E2-02 採樣（除頻排程）：Normal 分級每 8 tick 採集一次
// ===========================================================================
TEST(WifiStatusProvider, SampledViaE2_02Scheduler) {
    // 遞增訊號序列，供多次採集區分。
    std::vector<WifiStatus> seq;
    for (int i = 0; i < 10; ++i) {
        seq.push_back(WifiStatus::connected_to("N", -90.0 + i, 100.0 + i, 1,
                                               WifiBand::Band2_4GHz, WifiSecurity::WPA2));
    }
    auto src = std::make_shared<NullWifiSource>(seq);
    WifiStatusProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);  // 取第 1 份（history size = 1）

    SamplingScheduler sched;  // 預設 policy：Normal = 每 8 tick
    const auto id = WifiStatusProvider::kSignalMetricId;
    sched.add_demand(id, p.sampling_tier());  // Normal
    EXPECT_EQ(sched.effective_tier(id), SamplingTier::Normal);
    EXPECT_EQ(sched.effective_interval(id), 8u);

    // 推進到 t=16：Normal（間隔 8）於 t=0 首採、t=8、t=16 各一次；一次 advance 只採一份。
    int samples = 0;
    for (ds::metrics::Tick t = 0; t <= 16; ++t) {
        auto due = sched.advance(t);
        for (const auto& m : due) {
            if (m == id) {
                p.sample();
                ++samples;
            }
        }
    }
    EXPECT_EQ(samples, 3);  // t=0、8、16

    auto sig = reg.get("wifi.signal");
    // 歷史 = 1（register）+ 3（採樣）= 4。
    EXPECT_EQ(sig->single().history().size(), 4u);
}

// 除頻合併：兩消費者要同一指標，較高頻者勝出（High 覆蓋 Normal）。
TEST(WifiStatusProvider, DeFrequencyCoalescesToHighestTier) {
    WifiStatusProvider p{std::make_shared<NullWifiSource>(makeConnected())};
    SamplingScheduler sched;
    const auto id = WifiStatusProvider::kRssiMetricId;
    sched.add_demand(id, SamplingTier::Normal);
    sched.add_demand(id, SamplingTier::High);
    EXPECT_EQ(sched.demand_count(id), 2u);
    EXPECT_EQ(sched.effective_tier(id), SamplingTier::High);  // 除頻取最高頻
    EXPECT_EQ(sched.effective_interval(id), 1u);              // High = 每 1 tick
}

// ===========================================================================
// 重複註冊保守拒絕（不覆寫既有）
// ===========================================================================
TEST(WifiStatusProvider, DuplicateRegistrationRejected) {
    auto src = std::make_shared<NullWifiSource>(makeConnected());
    WifiStatusProvider p1{src};
    WifiStatusProvider p2{src};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 7u);
    EXPECT_EQ(reg.add_provider(p2), 0u);  // 七個 id 皆已存在 → 全被保守拒絕
    EXPECT_EQ(reg.size(), 7u);
}
