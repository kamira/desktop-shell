// E2-16 已安裝應用列舉 — 測試（gtest）
//
// 覆蓋：提供者身分、註冊到 E2-01 registry、null 後端回空清單、假資料注入列舉、
// 數量 = 實例數、清單 = 列舉實例、消費者只透過 E2-01 抽象介面走訪（不觸及具體型別）、
// 版本文字承載、null source 保守不崩、重複註冊保守拒絕。
// 相位 1：只驗介面 + null 後端行為，不含任何平台分支。
#include "installed_apps.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "metric.hpp"

using ds::metrics::Metric;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRegistry;
using ds::sysinfo::InstalledApp;
using ds::sysinfo::InstalledAppsProvider;
using ds::sysinfo::InstalledAppSource;
using ds::sysinfo::NullInstalledAppSource;

namespace {

// 建三個假應用的來源。
std::shared_ptr<NullInstalledAppSource> makeFakeSource() {
    auto src = std::make_shared<NullInstalledAppSource>();
    src->add_app(InstalledApp{"com.apple.Safari", "Safari", "17.0"});
    src->add_app(InstalledApp{"org.mozilla.firefox", "Firefox", "128.0"});
    src->add_app(InstalledApp{"com.example.tool", "Example Tool", ""});
    return src;
}

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(InstalledAppsProvider, ProviderIdIsStable) {
    InstalledAppsProvider p{std::make_shared<NullInstalledAppSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.apps");
    EXPECT_EQ(std::string(InstalledAppsProvider::kMetricId), "apps.installed");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(InstalledAppsProvider, IsAMetricProvider) {
    auto p = std::make_shared<InstalledAppsProvider>(std::make_shared<NullInstalledAppSource>());
    MetricProvider* base = p.get();  // 可上轉為 E2-01 抽象介面
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->provider_id(), "sysinfo.apps");
}

// ===========================================================================
// 註冊到 E2-01 registry
// ===========================================================================
TEST(InstalledAppsProvider, RegistersMetricViaRegistry) {
    MetricRegistry registry;
    InstalledAppsProvider provider{makeFakeSource()};

    const std::size_t added = registry.add_provider(provider);
    EXPECT_EQ(added, 1u);                      // 掛上一個指標
    EXPECT_TRUE(registry.contains("apps.installed"));
    EXPECT_EQ(registry.size(), 1u);
}

// ===========================================================================
// null 後端回空清單（不掃描系統）
// ===========================================================================
TEST(InstalledAppsProvider, NullBackendYieldsEmptyList) {
    // 預設 null 後端 = 空清單。
    NullInstalledAppSource src;
    EXPECT_TRUE(src.empty());
    EXPECT_TRUE(src.enumerate().empty());

    MetricRegistry registry;
    InstalledAppsProvider provider{std::make_shared<NullInstalledAppSource>()};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    auto metric = registry.get("apps.installed");
    ASSERT_NE(metric, nullptr);
    // 數量 = 0（空清單），但指標本身仍被掛上（消費者可查詢到「0 個」）。
    EXPECT_EQ(metric->instance_count(), 0u);
    EXPECT_EQ(metric->name(), "Installed Applications");
    EXPECT_EQ(metric->unit(), "");
}

// source 為 null 亦保守不崩，掛上空指標。
TEST(InstalledAppsProvider, NullSourcePointerIsSafe) {
    MetricRegistry registry;
    InstalledAppsProvider provider{nullptr};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("apps.installed");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 0u);
}

// ===========================================================================
// 假資料注入 → 列舉（數量 = 實例數、清單 = 列舉實例）
// ===========================================================================
TEST(InstalledAppsProvider, InjectedAppsAreEnumerated) {
    MetricRegistry registry;
    InstalledAppsProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    auto metric = registry.get("apps.installed");
    ASSERT_NE(metric, nullptr);

    // 「數量」= 實例數。
    EXPECT_EQ(metric->instance_count(), 3u);

    // 「清單」= 依序列舉實例（決定性 = 注入順序）。
    // 全程只用 E2-01 抽象 Metric / MetricInstance 介面，不觸及 sysinfo 具體型別。
    EXPECT_EQ(metric->instance(0).instance_id(), "com.apple.Safari");
    EXPECT_EQ(metric->instance(0).label(), "Safari");
    EXPECT_EQ(metric->instance(1).instance_id(), "org.mozilla.firefox");
    EXPECT_EQ(metric->instance(1).label(), "Firefox");
    EXPECT_EQ(metric->instance(2).instance_id(), "com.example.tool");
    EXPECT_EQ(metric->instance(2).label(), "Example Tool");
}

// 每個實例值 = 存在(1.0) + 版本文字。
TEST(InstalledAppsProvider, InstanceCarriesPresenceAndVersion) {
    MetricRegistry registry;
    InstalledAppsProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("apps.installed");
    ASSERT_NE(metric, nullptr);

    const auto v0 = metric->instance(0).value();
    EXPECT_TRUE(v0.valid);
    EXPECT_DOUBLE_EQ(v0.number, 1.0);        // 存在
    ASSERT_TRUE(v0.text.has_value());
    EXPECT_EQ(*v0.text, "17.0");             // Safari 版本

    // 版本未知（""）仍為有效存在值，文字為空字串。
    const auto v2 = metric->instance(2).value();
    EXPECT_TRUE(v2.valid);
    EXPECT_DOUBLE_EQ(v2.number, 1.0);
    ASSERT_TRUE(v2.text.has_value());
    EXPECT_EQ(*v2.text, "");
}

// find_instance 依 id 尋得（E2-01 便利查詢，走抽象介面）。
TEST(InstalledAppsProvider, FindInstanceById) {
    MetricRegistry registry;
    InstalledAppsProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("apps.installed");
    ASSERT_NE(metric, nullptr);

    const auto* firefox = metric->find_instance("org.mozilla.firefox");
    ASSERT_NE(firefox, nullptr);
    EXPECT_EQ(firefox->label(), "Firefox");
    EXPECT_EQ(metric->find_instance("does.not.exist"), nullptr);
}

// 範圍 = at_least(0)：下界 0、上無界。
TEST(InstalledAppsProvider, MetricRangeIsAtLeastZero) {
    MetricRegistry registry;
    InstalledAppsProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("apps.installed");
    ASSERT_NE(metric, nullptr);
    const auto r = metric->range();
    EXPECT_TRUE(r.has_min());
    EXPECT_FALSE(r.has_max());
    EXPECT_DOUBLE_EQ(*r.min, 0.0);
}

// ===========================================================================
// null 後端注入 API：set_apps / add_app / clear
// ===========================================================================
TEST(NullInstalledAppSource, InjectionApi) {
    NullInstalledAppSource src;
    EXPECT_EQ(src.size(), 0u);

    src.set_apps({InstalledApp{"a", "A", "1"}, InstalledApp{"b", "B", "2"}});
    EXPECT_EQ(src.size(), 2u);
    EXPECT_EQ(src.enumerate().size(), 2u);

    src.add_app(InstalledApp{"c", "C", ""});
    EXPECT_EQ(src.size(), 3u);

    src.clear();
    EXPECT_TRUE(src.empty());
    EXPECT_TRUE(src.enumerate().empty());
}

// 建構子注入。
TEST(NullInstalledAppSource, ConstructorInjection) {
    NullInstalledAppSource src{{InstalledApp{"x", "X", "9"}}};
    ASSERT_EQ(src.size(), 1u);
    EXPECT_EQ(src.enumerate()[0].name, "X");
}

// InstalledApp 相等性。
TEST(InstalledApp, Equality) {
    InstalledApp a{"id", "Name", "1.0"};
    InstalledApp b{"id", "Name", "1.0"};
    InstalledApp c{"id", "Name", "2.0"};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ===========================================================================
// 消費者範式：新增指標 = 新增提供者、消費者只走 E2-01 registry
// ===========================================================================
TEST(InstalledAppsProvider, ConsumerWalksViaAbstractContractOnly) {
    MetricRegistry registry;
    InstalledAppsProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    // 一個「掛件」風格的消費者：只認得 E2-01 的 Metric，數已安裝應用數量。
    std::size_t total = 0;
    for (const auto& m : registry.all()) {
        total += m->instance_count();   // 全程無 sysinfo 型別
    }
    EXPECT_EQ(total, 3u);
}

// 重複註冊保守拒絕（同 id 第二次掛上失敗、不覆寫）。
TEST(InstalledAppsProvider, DuplicateRegistrationRejected) {
    MetricRegistry registry;
    InstalledAppsProvider p1{makeFakeSource()};
    InstalledAppsProvider p2{std::make_shared<NullInstalledAppSource>()};

    EXPECT_EQ(registry.add_provider(p1), 1u);
    EXPECT_EQ(registry.add_provider(p2), 0u);   // 同 id "apps.installed" 被拒
    // 既有指標未被覆寫：仍為 p1 的三個實例。
    auto metric = registry.get("apps.installed");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 3u);
}

}  // namespace
