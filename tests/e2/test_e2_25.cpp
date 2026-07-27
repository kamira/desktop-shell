// E2-25 剪貼簿監看 — 測試（gtest）
//
// 覆蓋：快照型別語意、變更偵測器純邏輯（首觀察、變更/未變更、累計次數、重置）、
// 提供者身分、註冊到 E2-01 registry、null 後端空剪貼簿、注入內容採集驗值、
// 採集更新歷史、消費者只走 E2-01 抽象介面、E2-02 排程整合（Low 級需求 + 到期採集）、
// null source 保守不崩、重複註冊保守拒絕。
// 相位 1：只驗介面 + null 後端 + 純變更偵測邏輯，不含任何平台分支。
#include "clipboard_monitor.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "metric.hpp"
#include "sampling.hpp"

using ds::metrics::Metric;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRegistry;
using ds::metrics::SamplingScheduler;
using ds::metrics::SamplingTier;
using ds::sysinfo::ClipboardMonitor;
using ds::sysinfo::ClipboardMonitorProvider;
using ds::sysinfo::ClipboardSnapshot;
using ds::sysinfo::ClipboardSource;
using ds::sysinfo::NullClipboardSource;

namespace {

// ===========================================================================
// ClipboardSnapshot 型別語意
// ===========================================================================
TEST(ClipboardSnapshot, EmptyAndOfFactories) {
    const auto e = ClipboardSnapshot::empty();
    EXPECT_FALSE(e.present);
    EXPECT_TRUE(e.text.empty());

    const auto s = ClipboardSnapshot::of("hello");
    EXPECT_TRUE(s.present);
    EXPECT_EQ(s.text, "hello");
}

TEST(ClipboardSnapshot, Equality) {
    EXPECT_EQ(ClipboardSnapshot::of("a"), ClipboardSnapshot::of("a"));
    EXPECT_NE(ClipboardSnapshot::of("a"), ClipboardSnapshot::of("b"));
    EXPECT_NE(ClipboardSnapshot::of(""), ClipboardSnapshot::empty());  // present 不同
    EXPECT_EQ(ClipboardSnapshot::empty(), ClipboardSnapshot::empty());
}

// ===========================================================================
// ClipboardMonitor — 純邏輯變更偵測
// ===========================================================================
TEST(ClipboardMonitor, InitialState) {
    ClipboardMonitor m;
    EXPECT_FALSE(m.has_observed());
    EXPECT_EQ(m.change_count(), 0u);
    EXPECT_EQ(m.observed_count(), 0u);
    EXPECT_EQ(m.current(), ClipboardSnapshot::empty());
}

// 第一次觀察到非空內容 = 一次變更（相對初始空態）。
TEST(ClipboardMonitor, FirstNonEmptyIsChange) {
    ClipboardMonitor m;
    EXPECT_TRUE(m.observe(ClipboardSnapshot::of("x")));
    EXPECT_EQ(m.change_count(), 1u);
    EXPECT_EQ(m.observed_count(), 1u);
    EXPECT_EQ(m.current(), ClipboardSnapshot::of("x"));
}

// 第一次觀察即為空 = 與初始態相同，不算變更。
TEST(ClipboardMonitor, FirstEmptyIsNoChange) {
    ClipboardMonitor m;
    EXPECT_FALSE(m.observe(ClipboardSnapshot::empty()));
    EXPECT_EQ(m.change_count(), 0u);
    EXPECT_EQ(m.observed_count(), 1u);
    EXPECT_TRUE(m.has_observed());
}

// 相同內容重複觀察不累計；不同內容才累計。
TEST(ClipboardMonitor, DetectsChangesAndCoalescesRepeats) {
    ClipboardMonitor m;
    EXPECT_TRUE(m.observe(ClipboardSnapshot::of("a")));   // 變更 1
    EXPECT_FALSE(m.observe(ClipboardSnapshot::of("a")));  // 相同，不變
    EXPECT_FALSE(m.observe(ClipboardSnapshot::of("a")));  // 相同，不變
    EXPECT_TRUE(m.observe(ClipboardSnapshot::of("b")));   // 變更 2
    EXPECT_TRUE(m.observe(ClipboardSnapshot::empty()));   // 變更 3（清空也是變更）
    EXPECT_TRUE(m.observe(ClipboardSnapshot::of("b")));   // 變更 4（再變回 b）
    EXPECT_EQ(m.change_count(), 4u);
    EXPECT_EQ(m.observed_count(), 6u);
    EXPECT_EQ(m.current(), ClipboardSnapshot::of("b"));
}

// present 不同（空字串 vs 空剪貼簿）視為變更。
TEST(ClipboardMonitor, EmptyStringVsAbsentAreDistinct) {
    ClipboardMonitor m;
    EXPECT_TRUE(m.observe(ClipboardSnapshot::of("")));   // present=true, text=""
    EXPECT_EQ(m.change_count(), 1u);
    EXPECT_TRUE(m.observe(ClipboardSnapshot::empty()));  // present=false
    EXPECT_EQ(m.change_count(), 2u);
}

TEST(ClipboardMonitor, Reset) {
    ClipboardMonitor m;
    m.observe(ClipboardSnapshot::of("a"));
    m.observe(ClipboardSnapshot::of("b"));
    ASSERT_EQ(m.change_count(), 2u);
    m.reset();
    EXPECT_FALSE(m.has_observed());
    EXPECT_EQ(m.change_count(), 0u);
    EXPECT_EQ(m.observed_count(), 0u);
    EXPECT_EQ(m.current(), ClipboardSnapshot::empty());
}

// ===========================================================================
// NullClipboardSource — null 後端注入 API
// ===========================================================================
TEST(NullClipboardSource, DefaultIsEmpty) {
    NullClipboardSource src;
    EXPECT_TRUE(src.empty());
    EXPECT_EQ(src.read(), ClipboardSnapshot::empty());
}

TEST(NullClipboardSource, InjectionApi) {
    NullClipboardSource src;
    src.set_text("copied");
    EXPECT_FALSE(src.empty());
    EXPECT_EQ(src.read(), ClipboardSnapshot::of("copied"));

    src.set_snapshot(ClipboardSnapshot::of("other"));
    EXPECT_EQ(src.read().text, "other");

    src.clear();
    EXPECT_TRUE(src.empty());
    EXPECT_EQ(src.read(), ClipboardSnapshot::empty());
}

TEST(NullClipboardSource, ConstructorInjection) {
    NullClipboardSource src{ClipboardSnapshot::of("boot")};
    EXPECT_EQ(src.read().text, "boot");
}

// ===========================================================================
// 提供者身分（消費 E2-01 契約）
// ===========================================================================
TEST(ClipboardMonitorProvider, ProviderIdIsStable) {
    ClipboardMonitorProvider p{std::make_shared<NullClipboardSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.clipboard");
    EXPECT_EQ(std::string(ClipboardMonitorProvider::kMetricId), "clipboard.content");
    EXPECT_EQ(ClipboardMonitorProvider::kSuggestedTier, SamplingTier::Low);
}

TEST(ClipboardMonitorProvider, IsAMetricProvider) {
    auto p = std::make_shared<ClipboardMonitorProvider>(std::make_shared<NullClipboardSource>());
    MetricProvider* base = p.get();  // 可上轉為 E2-01 抽象介面
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->provider_id(), "sysinfo.clipboard");
}

// ===========================================================================
// 註冊到 E2-01 registry
// ===========================================================================
TEST(ClipboardMonitorProvider, RegistersMetricViaRegistry) {
    MetricRegistry registry;
    ClipboardMonitorProvider provider{std::make_shared<NullClipboardSource>()};

    const std::size_t added = registry.add_provider(provider);
    EXPECT_EQ(added, 1u);
    EXPECT_TRUE(registry.contains("clipboard.content"));

    auto metric = registry.get("clipboard.content");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->name(), "Clipboard");
    EXPECT_EQ(metric->unit(), "");
    EXPECT_EQ(metric->instance_count(), 1u);  // 單值來源 → 單一實例

    // 範圍 = at_least(0)（變更次數下界 0、上無界）。
    const auto r = metric->range();
    EXPECT_TRUE(r.has_min());
    EXPECT_FALSE(r.has_max());
    EXPECT_DOUBLE_EQ(*r.min, 0.0);
}

// 採集前：value 為 unknown（尚未讀值）。
TEST(ClipboardMonitorProvider, ValueUnknownBeforeSample) {
    MetricRegistry registry;
    ClipboardMonitorProvider provider{std::make_shared<NullClipboardSource>()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("clipboard.content");
    ASSERT_NE(metric, nullptr);
    EXPECT_FALSE(metric->single().value().valid);
}

// ===========================================================================
// 注入內容 → 採集驗值（透過 E2-01 抽象介面走訪）
// ===========================================================================
TEST(ClipboardMonitorProvider, SampleReflectsInjectedContent) {
    auto src = std::make_shared<NullClipboardSource>();
    src->set_text("first");
    ClipboardMonitorProvider provider{src};

    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);

    // 首採：偵測到變更（初始空 → "first"）。
    EXPECT_TRUE(provider.sample());
    EXPECT_EQ(provider.change_count(), 1u);

    auto metric = registry.get("clipboard.content");
    ASSERT_NE(metric, nullptr);
    const auto v = metric->single().value();
    EXPECT_TRUE(v.valid);
    EXPECT_DOUBLE_EQ(v.number, 1.0);                 // 累計變更次數
    ASSERT_TRUE(v.text.has_value());
    EXPECT_EQ(*v.text, "first");                     // 目前內容
}

// 內容變更 → 採集反映新值與新次數；歷史累積。
TEST(ClipboardMonitorProvider, SampleTracksChangesAndHistory) {
    auto src = std::make_shared<NullClipboardSource>();
    ClipboardMonitorProvider provider{src};
    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);

    src->set_text("a");
    EXPECT_TRUE(provider.sample());   // 變更 → 1
    src->set_text("a");
    EXPECT_FALSE(provider.sample());  // 未變更
    src->set_text("b");
    EXPECT_TRUE(provider.sample());   // 變更 → 2
    src->clear();
    EXPECT_TRUE(provider.sample());   // 清空亦變更 → 3

    EXPECT_EQ(provider.change_count(), 3u);

    auto metric = registry.get("clipboard.content");
    ASSERT_NE(metric, nullptr);
    const auto& inst = metric->single();
    const auto v = inst.value();
    EXPECT_DOUBLE_EQ(v.number, 3.0);
    ASSERT_TRUE(v.text.has_value());
    EXPECT_EQ(*v.text, "");           // 已清空 → 空字串

    // 歷史每次採集推一格：4 次採集 → 4 筆；最新 = 3。
    EXPECT_EQ(inst.history().size(), 4u);
    EXPECT_DOUBLE_EQ(inst.history().latest(), 3.0);
}

// ===========================================================================
// null 後端：空剪貼簿（不讀系統）
// ===========================================================================
TEST(ClipboardMonitorProvider, NullBackendEmptyClipboard) {
    ClipboardMonitorProvider provider{std::make_shared<NullClipboardSource>()};
    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);

    // 空剪貼簿首採：與初始空態相同 → 不算變更，但已採集（valid、內容為空）。
    EXPECT_FALSE(provider.sample());
    EXPECT_EQ(provider.change_count(), 0u);

    auto metric = registry.get("clipboard.content");
    ASSERT_NE(metric, nullptr);
    const auto v = metric->single().value();
    EXPECT_TRUE(v.valid);
    EXPECT_DOUBLE_EQ(v.number, 0.0);
    ASSERT_TRUE(v.text.has_value());
    EXPECT_EQ(*v.text, "");
}

// source 為 null 指標亦保守不崩：掛上指標、採集視為空剪貼簿。
TEST(ClipboardMonitorProvider, NullSourcePointerIsSafe) {
    ClipboardMonitorProvider provider{nullptr};
    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);
    EXPECT_FALSE(provider.sample());  // 視為空、不崩
    EXPECT_EQ(provider.change_count(), 0u);
}

// 未 register 就 sample：no-op、不崩。
TEST(ClipboardMonitorProvider, SampleBeforeRegisterIsNoop) {
    auto src = std::make_shared<NullClipboardSource>();
    src->set_text("x");
    ClipboardMonitorProvider provider{src};
    EXPECT_FALSE(provider.sample());          // 無實例 → no-op
    EXPECT_EQ(provider.change_count(), 0u);   // 變更偵測器未被驅動
    EXPECT_EQ(provider.metric(), nullptr);
}

// ===========================================================================
// E2-02 排程整合（除頻沿用 E2-02，不自造排程）
// ===========================================================================
TEST(ClipboardMonitorProvider, RegistersLowTierDemand) {
    ClipboardMonitorProvider provider{std::make_shared<NullClipboardSource>()};
    SamplingScheduler scheduler;  // 預設 policy：Low=每 64 tick

    const auto demand = provider.register_demand(scheduler);
    EXPECT_NE(demand, 0u);
    EXPECT_TRUE(scheduler.tracks("clipboard.content"));

    const auto tier = scheduler.effective_tier("clipboard.content");
    ASSERT_TRUE(tier.has_value());
    EXPECT_EQ(*tier, SamplingTier::Low);

    // Low 級預設間隔 64 tick。
    const auto interval = scheduler.effective_interval("clipboard.content");
    ASSERT_TRUE(interval.has_value());
    EXPECT_EQ(*interval, 64u);
}

// 排程驅動採集：到期 tick 時 clipboard.content 出現在 due 清單，據此 sample()。
TEST(ClipboardMonitorProvider, ScheduledSamplingDrivesUpdates) {
    auto src = std::make_shared<NullClipboardSource>();
    src->set_text("hello");
    ClipboardMonitorProvider provider{src};

    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);

    SamplingScheduler scheduler;
    provider.register_demand(scheduler);  // Low：間隔 64；新需求首採排在下一次 advance

    auto contains = [](const std::vector<ds::metrics::MetricId>& due) {
        for (const auto& id : due) {
            if (id == "clipboard.content") return true;
        }
        return false;
    };

    // 首採：新需求首採排在「下一次 advance 到達目前 tick」→ tick 1 即到期。
    ASSERT_TRUE(contains(scheduler.advance(1)));

    // 驅動採集。
    EXPECT_TRUE(provider.sample());
    EXPECT_EQ(provider.change_count(), 1u);
    EXPECT_EQ(*registry.get("clipboard.content")->single().value().text, "hello");

    // 首採後下次到期 = 1 + 64 = 65：中途（tick 40）不到期。
    EXPECT_FALSE(contains(scheduler.advance(40)));
    // 推進到 65：Low 級間隔屆滿，再次到期。
    EXPECT_TRUE(contains(scheduler.advance(65)));
}

// ===========================================================================
// 消費者範式：新增指標 = 新增提供者、消費者只走 E2-01 registry
// ===========================================================================
TEST(ClipboardMonitorProvider, ConsumerWalksViaAbstractContractOnly) {
    auto src = std::make_shared<NullClipboardSource>();
    src->set_text("payload");
    ClipboardMonitorProvider provider{src};
    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);
    provider.sample();

    // 掛件風格消費者：只認得 E2-01 的 Metric，讀變更次數，全程無 sysinfo 型別。
    double total_changes = 0.0;
    for (const auto& m : registry.all()) {
        if (m->is_single() && m->single().value().valid) {
            total_changes += m->single().value().number;
        }
    }
    EXPECT_DOUBLE_EQ(total_changes, 1.0);
}

// ===========================================================================
// 重複註冊保守拒絕
// ===========================================================================
TEST(ClipboardMonitorProvider, DuplicateRegistrationRejected) {
    MetricRegistry registry;
    ClipboardMonitorProvider p1{std::make_shared<NullClipboardSource>()};
    ClipboardMonitorProvider p2{std::make_shared<NullClipboardSource>()};

    EXPECT_EQ(registry.add_provider(p1), 1u);
    EXPECT_EQ(registry.add_provider(p2), 0u);  // 同 id "clipboard.content" 被拒

    // p2 掛上失敗 → 其 sample() 應為 no-op（不持有註冊表中物件、不崩）。
    EXPECT_FALSE(p2.sample());
    EXPECT_EQ(p2.metric(), nullptr);
}

}  // namespace
