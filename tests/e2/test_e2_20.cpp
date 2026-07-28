// E2-20 網路連通性 — 測試（gtest）
//
// 覆蓋：提供者身分、五個面向指標註冊到 E2-01 registry（online 單一實例 + reachable /
// latency / loss / dns 逐目標實例）、連通 / 斷線、延遲 ms、封包遺失率 %、多目標可達性、
// DNS 可解析、各面向獨立誠實 invalid、整體無讀值 invalid、經 E2-02 頻率採樣（除頻排程）、
// null 來源（固定 / 序列 / 空）、目標上線 / 下線、範圍（bool bounded(0,1) / latency
// at_least(0) / loss bounded(0,100)）、重複註冊保守拒絕、消費者只走 E2-01 抽象介面、
// 純函式 loss_pct_from_counts / online_from_targets。
// 相位 1：只驗介面 + 注入式來源行為，不含任何平台分支 / 真實探測。
#include "connectivity.hpp"

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
using ds::sysinfo::ConnectivityProvider;
using ds::sysinfo::ConnectivitySample;
using ds::sysinfo::ConnectivitySource;
using ds::sysinfo::ConnectivityTarget;
using ds::sysinfo::loss_pct_from_counts;
using ds::sysinfo::NullConnectivitySource;
using ds::sysinfo::online_from_targets;
using ds::sysinfo::OnlineVerdict;

namespace {

constexpr double kEps = 1e-9;

// 便利：組一份帶單一完整目標的固定快照。
ConnectivitySample make_sample(bool online, const std::string& target, bool reachable,
                               double latency_ms, double loss_pct, bool dns) {
    ConnectivitySample s;
    s.valid = true;
    s.online = online;
    s.online_valid = true;
    ConnectivityTarget t;
    t.name = target;
    t.reachable = reachable;
    t.reachable_valid = true;
    t.latency_ms = latency_ms;
    t.latency_valid = true;
    t.loss_pct = loss_pct;
    t.loss_valid = true;
    t.dns_resolvable = dns;
    t.dns_valid = true;
    s.targets.push_back(std::move(t));
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// 純函式：loss_pct_from_counts
// ---------------------------------------------------------------------------
TEST(LossPct, BasicRatio) {
    EXPECT_NEAR(loss_pct_from_counts(10, 7), 30.0, kEps);   // 遺失 3/10
    EXPECT_NEAR(loss_pct_from_counts(4, 1), 75.0, kEps);
    EXPECT_NEAR(loss_pct_from_counts(100, 100), 0.0, kEps); // 全收
    EXPECT_NEAR(loss_pct_from_counts(5, 0), 100.0, kEps);   // 全失
}

TEST(LossPct, Guards) {
    EXPECT_NEAR(loss_pct_from_counts(0, 0), 0.0, kEps);  // 未送出 → 0（不謊報）
    EXPECT_NEAR(loss_pct_from_counts(3, 9), 0.0, kEps);  // 收 > 送（保守夾住）→ 0
}

// ---------------------------------------------------------------------------
// 純函式：online_from_targets
// ---------------------------------------------------------------------------
TEST(OnlineFromTargets, AnyReachableIsOnline) {
    ConnectivitySample s;
    ConnectivityTarget a;
    a.name = "a";
    a.reachable = false;
    a.reachable_valid = true;
    ConnectivityTarget b;
    b.name = "b";
    b.reachable = true;
    b.reachable_valid = true;
    s.targets = {a, b};
    OnlineVerdict v = online_from_targets(s);
    EXPECT_TRUE(v.valid);
    EXPECT_TRUE(v.online);
}

TEST(OnlineFromTargets, AllUnreachableIsOfflineButValid) {
    ConnectivitySample s;
    ConnectivityTarget a;
    a.name = "a";
    a.reachable = false;
    a.reachable_valid = true;
    s.targets = {a};
    OnlineVerdict v = online_from_targets(s);
    EXPECT_TRUE(v.valid);
    EXPECT_FALSE(v.online);
}

TEST(OnlineFromTargets, NoValidReachabilityIsUnknown) {
    ConnectivitySample s;
    ConnectivityTarget a;  // reachable_valid == false
    a.name = "a";
    s.targets = {a};
    OnlineVerdict v = online_from_targets(s);
    EXPECT_FALSE(v.valid);  // 無任一有效可達性讀值 → 不謊報
    EXPECT_FALSE(v.online);
}

// ---------------------------------------------------------------------------
// 提供者身分 / metric_ids
// ---------------------------------------------------------------------------
TEST(Provider, IdentityAndMetricIds) {
    auto src = std::make_shared<NullConnectivitySource>();
    ConnectivityProvider p(src);
    EXPECT_EQ(p.provider_id(), "sysinfo.connectivity");
    EXPECT_EQ(p.sampling_tier(), SamplingTier::Normal);

    const auto ids = ConnectivityProvider::metric_ids();
    ASSERT_EQ(ids.size(), 5u);
    EXPECT_EQ(ids[0], "net.connectivity.online");
    EXPECT_EQ(ids[1], "net.connectivity.reachable");
    EXPECT_EQ(ids[2], "net.connectivity.latency");
    EXPECT_EQ(ids[3], "net.connectivity.loss");
    EXPECT_EQ(ids[4], "net.connectivity.dns");
}

// ---------------------------------------------------------------------------
// 註冊到 E2-01 registry：五個指標、單位、範圍
// ---------------------------------------------------------------------------
TEST(Provider, RegistersFiveMetricsWithUnitsAndRanges) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_online(true);
    src->set_target("8.8.8.8", /*reachable=*/true, /*latency=*/12.5, /*loss=*/0.0,
                    /*dns=*/true);

    MetricRegistry reg;
    ConnectivityProvider p(src);
    const std::size_t added = reg.add_provider(p);
    EXPECT_EQ(added, 5u);
    EXPECT_EQ(reg.size(), 5u);

    auto online = reg.get("net.connectivity.online");
    ASSERT_NE(online, nullptr);
    EXPECT_EQ(online->name(), "Network Online");
    EXPECT_EQ(online->unit(), "");
    EXPECT_TRUE(online->range().is_bounded());
    EXPECT_NEAR(*online->range().normalized(1.0), 1.0, kEps);

    auto latency = reg.get("net.connectivity.latency");
    ASSERT_NE(latency, nullptr);
    EXPECT_EQ(latency->unit(), "ms");
    EXPECT_TRUE(latency->range().has_min());
    EXPECT_FALSE(latency->range().has_max());

    auto loss = reg.get("net.connectivity.loss");
    ASSERT_NE(loss, nullptr);
    EXPECT_EQ(loss->unit(), "%");
    EXPECT_TRUE(loss->range().is_bounded());
    EXPECT_NEAR(*loss->range().max, 100.0, kEps);

    EXPECT_EQ(reg.get("net.connectivity.reachable")->unit(), "");
    EXPECT_EQ(reg.get("net.connectivity.dns")->unit(), "");
}

// ---------------------------------------------------------------------------
// online 為單一實例；逐目標指標以目標為實例
// ---------------------------------------------------------------------------
TEST(Provider, OnlineIsSingleInstanceTargetsEnumerated) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_online(true);
    src->set_target("8.8.8.8", true, 10.0, 0.0, true);
    src->set_target("1.1.1.1", true, 20.0, 0.0, true);

    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);

    auto online = reg.get("net.connectivity.online");
    EXPECT_TRUE(online->is_single());
    EXPECT_EQ(online->single().instance_id(), "internet");

    auto reachable = reg.get("net.connectivity.reachable");
    EXPECT_EQ(reachable->instance_count(), 2u);
    EXPECT_NE(reachable->find_instance("8.8.8.8"), nullptr);
    EXPECT_NE(reachable->find_instance("1.1.1.1"), nullptr);
    EXPECT_EQ(p.target_count(), 2u);
}

// ---------------------------------------------------------------------------
// 連通 / 斷線
// ---------------------------------------------------------------------------
TEST(Provider, OnlineAndOffline) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_online(true);
    src->set_target("8.8.8.8", /*reachable=*/true, 8.0, 0.0, true);

    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);

    auto online = reg.get("net.connectivity.online");
    ASSERT_TRUE(online->single().value().valid);
    EXPECT_NEAR(online->single().value().number, 1.0, kEps);
    EXPECT_NEAR(reg.get("net.connectivity.reachable")->find_instance("8.8.8.8")->value().number,
                1.0, kEps);

    // 轉為斷線。
    src->set_online(false);
    src->set_target("8.8.8.8", /*reachable=*/false, 0.0, 100.0, false);
    p.sample();
    EXPECT_NEAR(online->single().value().number, 0.0, kEps);
    EXPECT_NEAR(reg.get("net.connectivity.reachable")->find_instance("8.8.8.8")->value().number,
                0.0, kEps);
}

// ---------------------------------------------------------------------------
// 延遲 ms
// ---------------------------------------------------------------------------
TEST(Provider, Latency) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_target("8.8.8.8", true, /*latency=*/23.7, 0.0, true);

    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);

    auto latency = reg.get("net.connectivity.latency");
    ASSERT_TRUE(latency->find_instance("8.8.8.8")->value().valid);
    EXPECT_NEAR(latency->find_instance("8.8.8.8")->value().number, 23.7, kEps);
    // 有效讀值進入歷史。
    EXPECT_EQ(latency->find_instance("8.8.8.8")->history().size(), 1u);
}

// ---------------------------------------------------------------------------
// 封包遺失率 %
// ---------------------------------------------------------------------------
TEST(Provider, PacketLoss) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_target("8.8.8.8", true, 15.0, /*loss=*/42.0, true);

    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);

    auto loss = reg.get("net.connectivity.loss");
    ASSERT_TRUE(loss->find_instance("8.8.8.8")->value().valid);
    EXPECT_NEAR(loss->find_instance("8.8.8.8")->value().number, 42.0, kEps);
}

// ---------------------------------------------------------------------------
// 多目標可達性
// ---------------------------------------------------------------------------
TEST(Provider, MultiTargetReachability) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_online(true);
    src->set_target("8.8.8.8", /*reachable=*/true, 10.0, 0.0, true);
    src->set_target("10.0.0.1", /*reachable=*/false, 0.0, 100.0, false);

    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);

    auto reachable = reg.get("net.connectivity.reachable");
    EXPECT_EQ(reachable->instance_count(), 2u);
    EXPECT_NEAR(reachable->find_instance("8.8.8.8")->value().number, 1.0, kEps);
    EXPECT_NEAR(reachable->find_instance("10.0.0.1")->value().number, 0.0, kEps);
}

// ---------------------------------------------------------------------------
// DNS 可解析
// ---------------------------------------------------------------------------
TEST(Provider, DnsResolvable) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_target("dns.google", true, 5.0, 0.0, /*dns=*/true);
    src->set_target("bogus.invalid", false, 0.0, 100.0, /*dns=*/false);

    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);

    auto dns = reg.get("net.connectivity.dns");
    EXPECT_NEAR(dns->find_instance("dns.google")->value().number, 1.0, kEps);
    EXPECT_NEAR(dns->find_instance("bogus.invalid")->value().number, 0.0, kEps);
}

// ---------------------------------------------------------------------------
// 各面向獨立誠實 invalid：目標可達但延遲未量到
// ---------------------------------------------------------------------------
TEST(Provider, PerFacetHonestInvalid) {
    auto src = std::make_shared<NullConnectivitySource>();
    ConnectivitySample s;
    s.valid = true;
    s.online = true;
    s.online_valid = true;
    ConnectivityTarget t;
    t.name = "8.8.8.8";
    t.reachable = true;
    t.reachable_valid = true;
    t.latency_valid = false;  // 尚未量到延遲
    t.loss_valid = false;
    t.dns_resolvable = true;
    t.dns_valid = true;
    s.targets.push_back(t);
    src->set_sample(s);

    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);

    EXPECT_TRUE(reg.get("net.connectivity.reachable")->find_instance("8.8.8.8")->value().valid);
    EXPECT_FALSE(reg.get("net.connectivity.latency")->find_instance("8.8.8.8")->value().valid);
    EXPECT_FALSE(reg.get("net.connectivity.loss")->find_instance("8.8.8.8")->value().valid);
    EXPECT_TRUE(reg.get("net.connectivity.dns")->find_instance("8.8.8.8")->value().valid);
    // 未量到的面向不推入歷史（不污染序列）。
    EXPECT_EQ(reg.get("net.connectivity.latency")->find_instance("8.8.8.8")->history().size(),
              0u);
}

// ---------------------------------------------------------------------------
// 整體無讀值誠實 invalid（online + 所有目標未知）
// ---------------------------------------------------------------------------
TEST(Provider, OverallUnknownInvalid) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_online(true);
    src->set_target("8.8.8.8", true, 10.0, 0.0, true);

    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);
    ASSERT_TRUE(reg.get("net.connectivity.online")->single().value().valid);

    // 整份無讀值。
    src->clear();
    p.sample();
    EXPECT_FALSE(reg.get("net.connectivity.online")->single().value().valid);
    for (const auto& id : ConnectivityProvider::metric_ids()) {
        auto m = reg.get(id);
        if (m->is_single()) {
            EXPECT_FALSE(m->single().value().valid) << id;
        } else {
            EXPECT_FALSE(m->find_instance("8.8.8.8")->value().valid) << id;
        }
    }
}

// online_valid 為 false 但整體 valid：online 誠實未知，目標仍可有讀值。
TEST(Provider, OnlineFacetUnknownWhileTargetsValid) {
    auto src = std::make_shared<NullConnectivitySource>();
    ConnectivitySample s;
    s.valid = true;
    s.online_valid = false;  // 整體 online 尚未判定
    ConnectivityTarget t;
    t.name = "8.8.8.8";
    t.reachable = true;
    t.reachable_valid = true;
    s.targets.push_back(t);
    src->set_sample(s);

    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);

    EXPECT_FALSE(reg.get("net.connectivity.online")->single().value().valid);
    EXPECT_TRUE(reg.get("net.connectivity.reachable")->find_instance("8.8.8.8")->value().valid);
}

// ---------------------------------------------------------------------------
// 目標上線 / 下線：消失者設未知、不縮減、不污染歷史
// ---------------------------------------------------------------------------
TEST(Provider, TargetAppearsAndDisappears) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_sequence({
        make_sample(true, "8.8.8.8", true, 10.0, 0.0, true),
        // 第二份：8.8.8.8 消失、1.1.1.1 上線。
        make_sample(true, "1.1.1.1", true, 20.0, 0.0, true),
    });

    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);  // 消費第一份
    EXPECT_EQ(p.target_count(), 1u);

    p.sample();  // 消費第二份
    EXPECT_EQ(p.target_count(), 2u);  // 不縮減：8.8.8.8 保留為未知

    auto reachable = reg.get("net.connectivity.reachable");
    EXPECT_FALSE(reachable->find_instance("8.8.8.8")->value().valid);  // 消失者未知
    EXPECT_TRUE(reachable->find_instance("1.1.1.1")->value().valid);
    // 消失者不推入新歷史（維持首份的 1 筆）。
    EXPECT_EQ(reachable->find_instance("8.8.8.8")->history().size(), 1u);
    EXPECT_EQ(reachable->find_instance("1.1.1.1")->history().size(), 1u);
}

// ---------------------------------------------------------------------------
// null 來源：空列預設無讀值；序列逐份推進、列盡回最後一份
// ---------------------------------------------------------------------------
TEST(NullSource, EmptyReturnsUnknown) {
    NullConnectivitySource src;
    EXPECT_TRUE(src.empty());
    EXPECT_FALSE(src.sample().valid);
}

TEST(NullSource, SequenceAdvancesThenHoldsLast) {
    NullConnectivitySource src({
        make_sample(true, "a", true, 1.0, 0.0, true),
        make_sample(false, "a", false, 0.0, 100.0, false),
    });
    EXPECT_EQ(src.size(), 2u);
    EXPECT_TRUE(src.sample().online);   // 第一份
    EXPECT_FALSE(src.sample().online);  // 第二份
    EXPECT_FALSE(src.sample().online);  // 列盡 → 持續回最後一份
}

TEST(NullSource, FixedRepeats) {
    NullConnectivitySource src;
    src.set_online(true);
    src.set_target("8.8.8.8", true, 10.0, 0.0, true);
    // 固定：多次 sample() 回同一份。
    EXPECT_TRUE(src.sample().online);
    EXPECT_TRUE(src.sample().online);
    ASSERT_EQ(src.sample().targets.size(), 1u);
}

TEST(NullSource, ResetRewindsCursor) {
    NullConnectivitySource src({
        make_sample(true, "a", true, 1.0, 0.0, true),
        make_sample(false, "a", false, 0.0, 100.0, false),
    });
    src.sample();
    src.sample();
    src.reset();
    EXPECT_TRUE(src.sample().online);  // 回到起點
}

// null source 建構時無 source → 提供者仍掛滿五指標、保守無讀值。
TEST(Provider, NullSourceStillRegisters) {
    ConnectivityProvider p(nullptr);
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);
    EXPECT_EQ(added, 5u);
    EXPECT_EQ(p.target_count(), 0u);
    EXPECT_FALSE(reg.get("net.connectivity.online")->single().value().valid);
    p.sample();  // 不崩
}

// ---------------------------------------------------------------------------
// 經 E2-02 頻率採樣（除頻排程）
// ---------------------------------------------------------------------------
TEST(Sampling, DrivenByScheduler) {
    auto src = std::make_shared<NullConnectivitySource>();
    // 序列：每次 sample() 讀下一份延遲值。
    src->set_sequence({
        make_sample(true, "8.8.8.8", true, 10.0, 0.0, true),
        make_sample(true, "8.8.8.8", true, 11.0, 0.0, true),
        make_sample(true, "8.8.8.8", true, 12.0, 0.0, true),
    });

    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);  // 消費第一份（latency=10）

    SamplingScheduler sched;
    sched.add_demand(ConnectivityProvider::kLatencyMetricId, p.sampling_tier());
    ASSERT_TRUE(sched.effective_tier(ConnectivityProvider::kLatencyMetricId).has_value());

    const auto id = std::string(ConnectivityProvider::kLatencyMetricId);
    int samples = 0;
    for (ds::metrics::Tick t = 0; t <= 32; ++t) {
        auto due = sched.advance(t);
        for (const auto& m : due) {
            if (m == id) {
                p.sample();
                ++samples;
            }
        }
    }
    EXPECT_GT(samples, 0);
    // 最新延遲來自序列末份（列盡回最後一份 12.0）。
    auto latency = reg.get("net.connectivity.latency");
    EXPECT_NEAR(latency->find_instance("8.8.8.8")->value().number, 12.0, kEps);
    EXPECT_GT(latency->find_instance("8.8.8.8")->history().size(), 1u);
}

// 除頻合併：多需求同一指標 → 有效分級為最高頻者。
TEST(Sampling, CoalescesToHighestTier) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_target("8.8.8.8", true, 10.0, 0.0, true);
    ConnectivityProvider p(src);

    SamplingScheduler sched;
    sched.add_demand(ConnectivityProvider::kOnlineMetricId, SamplingTier::Low);
    sched.add_demand(ConnectivityProvider::kOnlineMetricId, SamplingTier::High);
    EXPECT_EQ(sched.effective_tier(ConnectivityProvider::kOnlineMetricId), SamplingTier::High);
}

// ---------------------------------------------------------------------------
// 重複註冊保守拒絕
// ---------------------------------------------------------------------------
TEST(Provider, DuplicateRegistrationRejected) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_online(true);
    MetricRegistry reg;
    ConnectivityProvider p1(src);
    ConnectivityProvider p2(src);
    EXPECT_EQ(reg.add_provider(p1), 5u);
    EXPECT_EQ(reg.add_provider(p2), 0u);  // 同 id，保守拒絕
    EXPECT_EQ(reg.size(), 5u);
}

// ---------------------------------------------------------------------------
// 消費者只走 E2-01 抽象介面（不觸及具體提供者型別）
// ---------------------------------------------------------------------------
TEST(Consumer, ThroughAbstractInterfaceOnly) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_online(true);
    src->set_target("8.8.8.8", true, 12.0, 0.0, true);
    MetricRegistry reg;
    ConnectivityProvider p(src);
    reg.add_provider(p);

    // 只透過 Metric / MetricInstance 抽象走訪。
    int bool_online = -1;
    for (const auto& m : reg.all()) {
        if (m->id() == "net.connectivity.online") {
            const Metric& metric = *m;
            bool_online = static_cast<int>(metric.single().value().number);
        }
    }
    EXPECT_EQ(bool_online, 1);
}

// sample() 未 register_metrics 時為 no-op（不崩）。
TEST(Provider, SampleBeforeRegisterIsNoop) {
    auto src = std::make_shared<NullConnectivitySource>();
    src->set_online(true);
    ConnectivityProvider p(src);
    p.sample();  // 尚未 register → no-op，不崩
    EXPECT_EQ(p.target_count(), 0u);
}
