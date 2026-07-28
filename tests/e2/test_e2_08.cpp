// E2-08 網路流量 — 測試（gtest）
//
// 覆蓋：提供者身分、六個面向指標註冊到 E2-01 registry、每介面實例列舉、
// 上傳/下載速率差分（注入兩份累積計數 + 時戳 → 算出 bytes/sec）、累積收發位元組、
// 封包數、多介面、首次差分速率 invalid（累積仍有效）、時間差為零處理、計數器重置 guard、
// 整體無讀值誠實 invalid、經 E2-02 頻率採樣（除頻排程）、null 來源行為、
// 介面上線 / 下線、消費者只走 E2-01 抽象介面、範圍 at_least(0)、重複註冊保守拒絕。
// 相位 1：只驗介面 + 注入式來源行為，不含任何平台分支。
#include "network_traffic.hpp"

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
using ds::sysinfo::DifferencingNetworkStatSource;
using ds::sysinfo::NetCountersSample;
using ds::sysinfo::NetInterfaceCounterEntry;
using ds::sysinfo::NetInterfaceCounters;
using ds::sysinfo::NetTrafficSample;
using ds::sysinfo::NetworkStatSource;
using ds::sysinfo::NetworkTrafficProvider;
using ds::sysinfo::NullNetCounterSource;
using ds::sysinfo::NullNetworkStatSource;
using ds::sysinfo::rate_from_delta;
using ds::sysinfo::traffic_from_delta;
using ds::sysinfo::traffic_from_snapshot;

namespace {

constexpr double kEps = 1e-9;

// 建一份累積計數快照：timestamp + 一列 {name, rx_bytes, tx_bytes, rx_pkts, tx_pkts}。
struct IfaceInit {
    std::string name;
    std::uint64_t rx_bytes;
    std::uint64_t tx_bytes;
    std::uint64_t rx_packets;
    std::uint64_t tx_packets;
};

NetCountersSample makeCounters(double ts, std::vector<IfaceInit> ifaces) {
    NetCountersSample s;
    s.timestamp = ts;
    for (auto& i : ifaces) {
        s.interfaces.push_back(
            NetInterfaceCounterEntry{i.name, {i.rx_bytes, i.tx_bytes, i.rx_packets,
                                              i.tx_packets}});
    }
    return s;
}

}  // namespace

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(NetworkTrafficProvider, ProviderIdAndMetricIds) {
    NetworkTrafficProvider p{std::make_shared<NullNetworkStatSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.net");
    EXPECT_EQ(std::string(NetworkTrafficProvider::kRxRateMetricId), "net.rx.rate");
    EXPECT_EQ(std::string(NetworkTrafficProvider::kTxRateMetricId), "net.tx.rate");
    EXPECT_EQ(std::string(NetworkTrafficProvider::kRxBytesMetricId), "net.rx.bytes");
    EXPECT_EQ(std::string(NetworkTrafficProvider::kTxBytesMetricId), "net.tx.bytes");
    EXPECT_EQ(std::string(NetworkTrafficProvider::kRxPacketsMetricId), "net.rx.packets");
    EXPECT_EQ(std::string(NetworkTrafficProvider::kTxPacketsMetricId), "net.tx.packets");

    auto ids = NetworkTrafficProvider::metric_ids();
    ASSERT_EQ(ids.size(), 6u);
    EXPECT_EQ(ids[0], "net.rx.rate");
    EXPECT_EQ(ids[5], "net.tx.packets");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(NetworkTrafficProvider, IsMetricProvider) {
    NetworkTrafficProvider p{std::make_shared<NullNetworkStatSource>()};
    MetricProvider& mp = p;
    EXPECT_EQ(mp.provider_id(), "sysinfo.net");
}

// 預設採集分級 = Normal，可由建構子覆寫。
TEST(NetworkTrafficProvider, SamplingTierDefaultsNormalOverridable) {
    NetworkTrafficProvider def{std::make_shared<NullNetworkStatSource>()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::Normal);

    NetworkTrafficProvider high{std::make_shared<NullNetworkStatSource>(),
                                NetworkTrafficProvider::kDefaultHistory,
                                SamplingTier::High};
    EXPECT_EQ(high.sampling_tier(), SamplingTier::High);
}

// ===========================================================================
// 註冊：六個面向指標
// ===========================================================================
TEST(NetworkTrafficProvider, RegistersSixMetrics) {
    NetworkTrafficProvider p{std::make_shared<NullNetworkStatSource>()};
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);

    EXPECT_EQ(added, 6u);
    for (const auto& id : NetworkTrafficProvider::metric_ids()) {
        EXPECT_TRUE(reg.contains(id)) << id;
    }
    // 單位正確。
    EXPECT_EQ(reg.get("net.rx.rate")->unit(), "B/s");
    EXPECT_EQ(reg.get("net.rx.bytes")->unit(), "B");
    EXPECT_EQ(reg.get("net.rx.packets")->unit(), "packets");
}

// ===========================================================================
// 多介面：每介面實例列舉
// ===========================================================================
TEST(NetworkTrafficProvider, MultiInterfaceInstances) {
    auto src = std::make_shared<NullNetworkStatSource>();
    src->set_interface("en0", 100.0, 50.0, 1000, 500, 10, 5);
    src->set_interface("en1", 0.0, 0.0, 20, 30, 2, 3);
    NetworkTrafficProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    EXPECT_EQ(p.interface_count(), 2u);
    auto rx_rate = reg.get("net.rx.rate");
    ASSERT_NE(rx_rate, nullptr);
    ASSERT_EQ(rx_rate->instance_count(), 2u);
    // 列舉順序 = 首見序。
    EXPECT_EQ(rx_rate->instance(0).instance_id(), "en0");
    EXPECT_EQ(rx_rate->instance(0).label(), "en0");
    EXPECT_EQ(rx_rate->instance(1).instance_id(), "en1");

    // 六個指標都有相同的兩個介面實例。
    for (const auto& id : NetworkTrafficProvider::metric_ids()) {
        EXPECT_EQ(reg.get(id)->instance_count(), 2u) << id;
    }
}

// 上傳/下載速率 + 累積位元組 + 封包數讀值正確（來源直接給流量路徑）。
TEST(NetworkTrafficProvider, DirectSourceValues) {
    auto src = std::make_shared<NullNetworkStatSource>();
    src->set_interface("en0", /*rx_rate=*/1234.0, /*tx_rate=*/567.0,
                       /*rx_bytes=*/9000, /*tx_bytes=*/4000,
                       /*rx_packets=*/70, /*tx_packets=*/40);
    NetworkTrafficProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    EXPECT_NEAR(reg.get("net.rx.rate")->find_instance("en0")->value().number, 1234.0, kEps);
    EXPECT_NEAR(reg.get("net.tx.rate")->find_instance("en0")->value().number, 567.0, kEps);
    EXPECT_NEAR(reg.get("net.rx.bytes")->find_instance("en0")->value().number, 9000.0, kEps);
    EXPECT_NEAR(reg.get("net.tx.bytes")->find_instance("en0")->value().number, 4000.0, kEps);
    EXPECT_NEAR(reg.get("net.rx.packets")->find_instance("en0")->value().number, 70.0, kEps);
    EXPECT_NEAR(reg.get("net.tx.packets")->find_instance("en0")->value().number, 40.0, kEps);
    // 全部有效。
    EXPECT_TRUE(reg.get("net.rx.rate")->find_instance("en0")->value().valid);
}

// ===========================================================================
// 速率差分：注入兩份累積計數 + 時戳 → bytes/sec
// ===========================================================================
TEST(RateFromDelta, BytesPerSecond) {
    // 1000 位元組 / 2 秒 = 500 B/s。
    EXPECT_NEAR(rate_from_delta(500, 1500, 2.0), 500.0, kEps);
    // 無經過時間 → 0。
    EXPECT_NEAR(rate_from_delta(500, 1500, 0.0), 0.0, kEps);
    EXPECT_NEAR(rate_from_delta(500, 1500, -1.0), 0.0, kEps);
    // 計數器重置（curr < prev）→ 0。
    EXPECT_NEAR(rate_from_delta(2000, 100, 1.0), 0.0, kEps);
}

TEST(TrafficFromDelta, PerInterfaceRates) {
    // en0：rx 從 1000→3000（Δ2000）/ 2s = 1000 B/s；tx 500→900（Δ400）/ 2s = 200 B/s。
    NetCountersSample prev = makeCounters(10.0, {{"en0", 1000, 500, 10, 5}});
    NetCountersSample curr = makeCounters(12.0, {{"en0", 3000, 900, 30, 9}});
    NetTrafficSample t = traffic_from_delta(prev, curr);

    ASSERT_TRUE(t.valid);
    ASSERT_EQ(t.interface_count(), 1u);
    const auto* f = t.find("en0");
    ASSERT_NE(f, nullptr);
    EXPECT_TRUE(f->rate_valid);
    EXPECT_NEAR(f->rx_rate, 1000.0, kEps);
    EXPECT_NEAR(f->tx_rate, 200.0, kEps);
    // 累積計數取自 curr。
    EXPECT_EQ(f->rx_bytes, 3000u);
    EXPECT_EQ(f->tx_packets, 9u);
}

// 經提供者：兩次取樣差分（累積計數來源 → 差分轉接器）。
TEST(NetworkTrafficProvider, DifferencingThroughProvider) {
    auto counters = std::make_shared<NullNetCounterSource>(std::vector<NetCountersSample>{
        makeCounters(0.0, {{"en0", 0, 0, 0, 0}}),
        makeCounters(1.0, {{"en0", 4096, 2048, 40, 20}}),
    });
    auto diff = std::make_shared<DifferencingNetworkStatSource>(counters);
    NetworkTrafficProvider p{diff};
    MetricRegistry reg;
    reg.add_provider(p);  // register 內首次 sample → 只有一份 → 速率未知、累積有效

    auto rx_rate = reg.get("net.rx.rate");
    auto rx_bytes = reg.get("net.rx.bytes");
    ASSERT_NE(rx_rate, nullptr);
    // 首次差分：速率 invalid（首份無可差分基準）。
    EXPECT_FALSE(rx_rate->find_instance("en0")->value().valid);
    // 但累積位元組首份即有效（直接可讀）。
    ASSERT_TRUE(rx_bytes->find_instance("en0")->value().valid);
    EXPECT_NEAR(rx_bytes->find_instance("en0")->value().number, 0.0, kEps);

    // 第二次取樣：讀第二份、與第一份差分 → rx 4096 B / 1s = 4096 B/s。
    p.sample();
    ASSERT_TRUE(rx_rate->find_instance("en0")->value().valid);
    EXPECT_NEAR(rx_rate->find_instance("en0")->value().number, 4096.0, kEps);
    EXPECT_NEAR(reg.get("net.tx.rate")->find_instance("en0")->value().number, 2048.0, kEps);
    // 累積更新到第二份。
    EXPECT_NEAR(rx_bytes->find_instance("en0")->value().number, 4096.0, kEps);
    EXPECT_NEAR(reg.get("net.rx.packets")->find_instance("en0")->value().number, 40.0, kEps);
}

// ===========================================================================
// 首次差分 invalid（速率）；時間差為零處理
// ===========================================================================
TEST(DifferencingNetworkStatSource, FirstSampleRateUnknownCumulativeValid) {
    auto counters = std::make_shared<NullNetCounterSource>(std::vector<NetCountersSample>{
        makeCounters(0.0, {{"en0", 111, 222, 1, 2}}),
        makeCounters(5.0, {{"en0", 611, 722, 11, 12}}),
    });
    DifferencingNetworkStatSource diff{counters};
    EXPECT_FALSE(diff.primed());

    NetTrafficSample first = diff.sample();  // 首份
    EXPECT_TRUE(diff.primed());
    ASSERT_TRUE(first.valid);                // 整體有讀值
    const auto* f0 = first.find("en0");
    ASSERT_NE(f0, nullptr);
    EXPECT_FALSE(f0->rate_valid);            // 速率首次 → invalid
    EXPECT_EQ(f0->rx_bytes, 111u);           // 累積首份即有效

    NetTrafficSample second = diff.sample();  // 差分：rxΔ500 / 5s = 100 B/s
    const auto* f1 = second.find("en0");
    ASSERT_TRUE(f1->rate_valid);
    EXPECT_NEAR(f1->rx_rate, 100.0, kEps);

    // 無 counter 來源 → 無讀值。
    DifferencingNetworkStatSource none{nullptr};
    EXPECT_FALSE(none.sample().valid);
}

// 時間差為零：兩份時戳相同 → 速率 invalid（不謊報），累積仍有效。
TEST(TrafficFromDelta, ZeroTimeDeltaRateInvalidCumulativeValid) {
    NetCountersSample prev = makeCounters(7.0, {{"en0", 1000, 500, 10, 5}});
    NetCountersSample curr = makeCounters(7.0, {{"en0", 3000, 900, 30, 9}});
    NetTrafficSample t = traffic_from_delta(prev, curr);

    ASSERT_TRUE(t.valid);
    const auto* f = t.find("en0");
    ASSERT_NE(f, nullptr);
    EXPECT_FALSE(f->rate_valid);       // dt==0 → 速率無從算
    EXPECT_NEAR(f->rx_rate, 0.0, kEps);
    EXPECT_EQ(f->rx_bytes, 3000u);     // 累積仍取自 curr
}

// 計數器重置：curr 累積 < prev（回繞 / 重置）→ 速率 0，rate_valid 仍 true（有 dt）。
TEST(TrafficFromDelta, CounterResetGuard) {
    NetCountersSample prev = makeCounters(0.0, {{"en0", 5000, 5000, 50, 50}});
    NetCountersSample curr = makeCounters(1.0, {{"en0", 10, 10, 1, 1}});  // 重置
    NetTrafficSample t = traffic_from_delta(prev, curr);
    const auto* f = t.find("en0");
    ASSERT_NE(f, nullptr);
    EXPECT_TRUE(f->rate_valid);
    EXPECT_NEAR(f->rx_rate, 0.0, kEps);  // 重置 → 保守 0，不爆量
    EXPECT_NEAR(f->tx_rate, 0.0, kEps);
}

// ===========================================================================
// 多介面差分：新上線介面只有累積、暫無速率
// ===========================================================================
TEST(TrafficFromDelta, NewInterfaceHasCumulativeButNoRate) {
    NetCountersSample prev = makeCounters(0.0, {{"en0", 100, 100, 1, 1}});
    // curr 多一個 en1（prev 無對應）。
    NetCountersSample curr =
        makeCounters(1.0, {{"en0", 300, 300, 3, 3}, {"en1", 999, 888, 9, 8}});
    NetTrafficSample t = traffic_from_delta(prev, curr);

    ASSERT_EQ(t.interface_count(), 2u);
    const auto* en0 = t.find("en0");
    const auto* en1 = t.find("en1");
    ASSERT_NE(en0, nullptr);
    ASSERT_NE(en1, nullptr);
    // en0 有前份 → 速率有效（rxΔ200/1s=200）。
    EXPECT_TRUE(en0->rate_valid);
    EXPECT_NEAR(en0->rx_rate, 200.0, kEps);
    // en1 新上線 → 只有累積、速率 invalid。
    EXPECT_FALSE(en1->rate_valid);
    EXPECT_EQ(en1->rx_bytes, 999u);
}

// 介面上線 / 下線經提供者：下線介面實例設未知（不縮減）。
TEST(NetworkTrafficProvider, InterfaceAppearsThenDisappears) {
    auto src = std::make_shared<NullNetworkStatSource>();
    src->set_interface("en0", 10.0, 10.0, 100, 100, 1, 1);
    src->set_interface("en1", 20.0, 20.0, 200, 200, 2, 2);
    NetworkTrafficProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.interface_count(), 2u);

    // en1 消失（只留 en0）。
    NetTrafficSample only_en0;
    only_en0.valid = true;
    only_en0.interfaces.push_back({"en0", 30.0, 30.0, 300, 300, 3, 3, true});
    src->set_traffic(only_en0);
    p.sample();

    // 實例數不縮減（en1 仍在）。
    EXPECT_EQ(p.interface_count(), 2u);
    auto rx_rate = reg.get("net.rx.rate");
    EXPECT_TRUE(rx_rate->find_instance("en0")->value().valid);
    EXPECT_NEAR(rx_rate->find_instance("en0")->value().number, 30.0, kEps);
    // en1 下線 → 全面向未知。
    EXPECT_FALSE(rx_rate->find_instance("en1")->value().valid);
    EXPECT_FALSE(reg.get("net.tx.bytes")->find_instance("en1")->value().valid);
}

// 新介面在後續取樣才出現 → 動態新增實例（既有參照不失效）。
TEST(NetworkTrafficProvider, InterfaceAddedOnLaterSample) {
    auto src = std::make_shared<NullNetworkStatSource>();
    src->set_interface("en0", 10.0, 10.0, 100, 100, 1, 1);
    NetworkTrafficProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.interface_count(), 1u);
    const auto* en0_before = reg.get("net.rx.rate")->find_instance("en0");

    src->set_interface("en1", 20.0, 20.0, 200, 200, 2, 2);  // 新介面上線
    p.sample();
    EXPECT_EQ(p.interface_count(), 2u);
    // 既有 en0 參照仍有效。
    EXPECT_EQ(reg.get("net.rx.rate")->find_instance("en0"), en0_before);
    EXPECT_NE(reg.get("net.rx.rate")->find_instance("en1"), nullptr);
}

// ===========================================================================
// 經 E2-02 頻率採樣（除頻排程）
// ===========================================================================
TEST(NetworkTrafficProvider, SampledViaE2_02Scheduler) {
    // 累積計數序列：每份 rx 各 +1000、tx 各 +500，時戳每份 +1s（→ 每次差分 rx=1000 B/s）。
    std::vector<NetCountersSample> seq;
    for (std::uint64_t k = 0; k <= 40; ++k) {
        seq.push_back(makeCounters(static_cast<double>(k),
                                   {{"en0", 1000 * k, 500 * k, 10 * k, 5 * k}}));
    }
    auto counters = std::make_shared<NullNetCounterSource>(std::move(seq));
    auto diff = std::make_shared<DifferencingNetworkStatSource>(counters);
    NetworkTrafficProvider p{diff};
    MetricRegistry reg;
    reg.add_provider(p);  // register 消耗第 0 份（基準）

    SamplingScheduler sched;
    sched.add_demand(NetworkTrafficProvider::kRxRateMetricId, p.sampling_tier());
    // 有效分級 = Normal（間隔 8）。
    ASSERT_TRUE(sched.effective_tier(NetworkTrafficProvider::kRxRateMetricId).has_value());
    EXPECT_EQ(*sched.effective_tier(NetworkTrafficProvider::kRxRateMetricId),
              SamplingTier::Normal);

    int sampled = 0;
    for (ds::metrics::Tick t = 1; t <= 32; ++t) {
        auto due = sched.advance(t);
        for (const auto& id : due) {
            if (id == NetworkTrafficProvider::kRxRateMetricId) {
                p.sample();
                ++sampled;
            }
        }
    }
    // Normal 間隔 8：32 tick 內採 4 次（t=8,16,24,32）。
    EXPECT_EQ(sampled, 4);

    auto rx_rate = reg.get("net.rx.rate");
    const auto& hist = rx_rate->find_instance("en0")->history();
    EXPECT_EQ(hist.size(), 4u);
    EXPECT_NEAR(hist.latest(), 1000.0, kEps);
}

// 除頻：多消費者同一指標合併，最高頻者供給。
TEST(NetworkTrafficProvider, DeFrequencyCoalescesDemands) {
    SamplingScheduler sched;
    auto d_low = sched.add_demand(NetworkTrafficProvider::kRxRateMetricId, SamplingTier::Low);
    sched.add_demand(NetworkTrafficProvider::kRxRateMetricId, SamplingTier::High);
    EXPECT_EQ(*sched.effective_tier(NetworkTrafficProvider::kRxRateMetricId),
              SamplingTier::High);
    EXPECT_EQ(sched.demand_count(NetworkTrafficProvider::kRxRateMetricId), 2u);

    EXPECT_TRUE(sched.remove_demand(d_low));
    EXPECT_TRUE(sched.tracks(NetworkTrafficProvider::kRxRateMetricId));
    EXPECT_EQ(*sched.effective_tier(NetworkTrafficProvider::kRxRateMetricId),
              SamplingTier::High);
}

// ===========================================================================
// null 來源行為 / 無讀值誠實 invalid
// ===========================================================================
TEST(NetworkTrafficProvider, NullSourceIsConservative) {
    // source 為 null 指標：仍掛上六個指標，無介面實例、不崩。
    NetworkTrafficProvider p{nullptr};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 6u);
    for (const auto& id : NetworkTrafficProvider::metric_ids()) {
        auto m = reg.get(id);
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(m->instance_count(), 0u);  // 無介面
    }
    EXPECT_EQ(p.interface_count(), 0u);
    p.sample();  // 不崩
    EXPECT_EQ(p.interface_count(), 0u);
}

TEST(NetworkTrafficProvider, NullStatSourceDefaultUnknownThenInjected) {
    auto src = std::make_shared<NullNetworkStatSource>();  // 預設無讀值
    NetworkTrafficProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_EQ(p.interface_count(), 0u);

    // 注入資料 → sample() 後可讀。
    src->set_interface("en0", 42.0, 0.0, 500, 0, 5, 0);
    p.sample();
    EXPECT_EQ(p.interface_count(), 1u);
    EXPECT_TRUE(reg.get("net.rx.rate")->find_instance("en0")->value().valid);
    EXPECT_NEAR(reg.get("net.rx.rate")->find_instance("en0")->value().number, 42.0, kEps);
}

// 整體無讀值：既有介面實例全面向設未知（不謊報 0）。
TEST(NetworkTrafficProvider, ReadingLostMarksAllUnknown) {
    auto src = std::make_shared<NullNetworkStatSource>();
    src->set_interface("en0", 10.0, 10.0, 100, 100, 1, 1);
    NetworkTrafficProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);
    EXPECT_TRUE(reg.get("net.rx.rate")->find_instance("en0")->value().valid);

    src->clear();  // 回到無讀值
    p.sample();
    // 全面向未知（不縮減實例、不謊報 0）。
    for (const auto& id : NetworkTrafficProvider::metric_ids()) {
        EXPECT_FALSE(reg.get(id)->find_instance("en0")->value().valid) << id;
    }
}

// sample() 未 register_metrics 時為 no-op（不崩）。
TEST(NetworkTrafficProvider, SampleBeforeRegisterIsNoop) {
    auto src = std::make_shared<NullNetworkStatSource>();
    src->set_interface("en0", 1.0, 1.0, 1, 1, 1, 1);
    NetworkTrafficProvider p{src};
    p.sample();  // 尚未 register → no-op，不崩
    EXPECT_EQ(p.interface_count(), 0u);
}

// NullNetCounterSource：列盡持續回最後一份；空列回空快照。
TEST(NullNetCounterSource, ExhaustionAndEmpty) {
    NullNetCounterSource src{std::vector<NetCountersSample>{
        makeCounters(1.0, {{"en0", 1, 2, 3, 4}})}};
    EXPECT_EQ(src.read().interface_count(), 1u);
    // 列盡 → 持續回最後一份。
    auto again = src.read();
    ASSERT_EQ(again.interface_count(), 1u);
    EXPECT_EQ(again.interfaces[0].counters, (NetInterfaceCounters{1, 2, 3, 4}));

    NullNetCounterSource empty;
    EXPECT_TRUE(empty.read().empty());
}

// traffic_from_snapshot：累積有效、速率 invalid。
TEST(TrafficFromSnapshot, CumulativeValidRateInvalid) {
    NetCountersSample s = makeCounters(3.0, {{"en0", 10, 20, 1, 2}});
    NetTrafficSample t = traffic_from_snapshot(s);
    ASSERT_TRUE(t.valid);
    const auto* f = t.find("en0");
    ASSERT_NE(f, nullptr);
    EXPECT_FALSE(f->rate_valid);
    EXPECT_EQ(f->rx_bytes, 10u);
    EXPECT_EQ(f->tx_packets, 2u);
}

// ===========================================================================
// 範圍 / 消費者範式 / 重複註冊
// ===========================================================================
TEST(NetworkTrafficProvider, RangeIsAtLeastZero) {
    NetworkTrafficProvider p{std::make_shared<NullNetworkStatSource>()};
    MetricRegistry reg;
    reg.add_provider(p);
    for (const auto& id : NetworkTrafficProvider::metric_ids()) {
        auto r = reg.get(id)->range();
        EXPECT_TRUE(r.has_min());
        EXPECT_FALSE(r.has_max());  // 上無界（速率 / 累積量無理論上限）
        EXPECT_NEAR(*r.min, 0.0, kEps);
    }
}

// 掛件風格消費者：只透過 E2-01 registry / Metric 抽象介面走訪，完全不觸及具體型別。
TEST(NetworkTrafficProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = std::make_shared<NullNetworkStatSource>();
    src->set_interface("en0", 100.0, 0.0, 0, 0, 0, 0);
    src->set_interface("en1", 300.0, 0.0, 0, 0, 0, 0);
    NetworkTrafficProvider p{src};
    MetricRegistry reg;
    reg.add_provider(p);

    std::shared_ptr<Metric> m = reg.get("net.rx.rate");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), "B/s");
    double sum = 0.0;
    for (std::size_t i = 0; i < m->instance_count(); ++i) {
        const auto& inst = m->instance(i);
        if (inst.value().valid) sum += inst.value().number;
    }
    EXPECT_NEAR(sum, 400.0, kEps);  // 100 + 300
}

TEST(NetworkTrafficProvider, DuplicateRegistrationRejected) {
    NetworkTrafficProvider p1{std::make_shared<NullNetworkStatSource>()};
    NetworkTrafficProvider p2{std::make_shared<NullNetworkStatSource>()};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 6u);
    // 第二個提供者掛同一批 id → 全數保守拒絕（不覆寫既有）。
    EXPECT_EQ(reg.add_provider(p2), 0u);
    EXPECT_EQ(reg.size(), 6u);
}

// ===========================================================================
// 值模型單元
// ===========================================================================
TEST(NetTrafficSample, UnknownDefault) {
    NetTrafficSample u = NetTrafficSample::unknown();
    EXPECT_FALSE(u.valid);
    EXPECT_EQ(u.interface_count(), 0u);
    EXPECT_EQ(u.find("en0"), nullptr);
}

TEST(NetInterfaceCounters, Equality) {
    NetInterfaceCounters a{1, 2, 3, 4};
    NetInterfaceCounters b{1, 2, 3, 4};
    NetInterfaceCounters c{1, 2, 3, 5};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}
