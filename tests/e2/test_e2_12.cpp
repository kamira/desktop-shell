// E2-12 系統靜態資訊 — 測試（gtest）
//
// 覆蓋：提供者身分、註冊到 E2-01 registry、null 後端回空欄位集、假資料注入列舉、
// 多欄位（欄位數 = 實例數、欄位集 = 列舉實例）、文字 + 數值維度承載、
// 消費者只透過 E2-01 抽象介面走訪（不觸及具體型別）、null source 保守不崩、
// 範圍 unbounded、無歷史（history_capacity=0）、null 後端注入 API、重複註冊保守拒絕。
// 相位 1：只驗介面 + null 後端行為，不含任何平台分支。
#include "system_info.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "metric.hpp"

using ds::metrics::Metric;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRegistry;
using ds::sysinfo::NullSysInfoSource;
using ds::sysinfo::SysInfoField;
using ds::sysinfo::SysInfoSource;
using ds::sysinfo::SystemInfoProvider;

namespace {

// 建一組平台中立的假靜態欄位（OS 名稱/版本、主機名、CPU 型號、核心數）。
std::shared_ptr<NullSysInfoSource> makeFakeSource() {
    auto src = std::make_shared<NullSysInfoSource>();
    src->add_field(SysInfoField{"os.name", "OS Name", "macOS", 0.0});
    src->add_field(SysInfoField{"os.version", "OS Version", "14.5", 0.0});
    src->add_field(SysInfoField{"host.name", "Hostname", "dev-machine", 0.0});
    src->add_field(SysInfoField{"cpu.model", "CPU Model", "Apple M1", 0.0});
    src->add_field(SysInfoField{"cpu.cores", "CPU Cores", "8", 8.0});
    return src;
}

// ===========================================================================
// 提供者身分
// ===========================================================================
TEST(SystemInfoProvider, ProviderIdIsStable) {
    SystemInfoProvider p{std::make_shared<NullSysInfoSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.system");
    EXPECT_EQ(std::string(SystemInfoProvider::kMetricId), "system.static");
    EXPECT_EQ(std::string(SystemInfoProvider::kMetricName), "System Information");
}

// 消費 E2-01 契約：本提供者確為 MetricProvider（介面契約，非自造模型）。
TEST(SystemInfoProvider, IsAMetricProvider) {
    auto p = std::make_shared<SystemInfoProvider>(std::make_shared<NullSysInfoSource>());
    MetricProvider* base = p.get();  // 可上轉為 E2-01 抽象介面
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->provider_id(), "sysinfo.system");
}

// ===========================================================================
// 註冊到 E2-01 registry
// ===========================================================================
TEST(SystemInfoProvider, RegistersMetricViaRegistry) {
    MetricRegistry registry;
    SystemInfoProvider provider{makeFakeSource()};

    const std::size_t added = registry.add_provider(provider);
    EXPECT_EQ(added, 1u);                       // 掛上一個指標
    EXPECT_TRUE(registry.contains("system.static"));
    EXPECT_EQ(registry.size(), 1u);
}

// ===========================================================================
// null 後端回空欄位集（不查系統）
// ===========================================================================
TEST(SystemInfoProvider, NullBackendYieldsEmptyFields) {
    // 預設 null 後端 = 空欄位集。
    NullSysInfoSource src;
    EXPECT_TRUE(src.empty());
    EXPECT_TRUE(src.query().empty());

    MetricRegistry registry;
    SystemInfoProvider provider{std::make_shared<NullSysInfoSource>()};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    auto metric = registry.get("system.static");
    ASSERT_NE(metric, nullptr);
    // 欄位數 = 0（空），但指標本身仍被掛上（消費者可查詢到「0 個欄位」）。
    EXPECT_EQ(metric->instance_count(), 0u);
    EXPECT_EQ(metric->name(), "System Information");
    EXPECT_EQ(metric->unit(), "");
}

// source 為 null 亦保守不崩，掛上空指標。
TEST(SystemInfoProvider, NullSourcePointerIsSafe) {
    MetricRegistry registry;
    SystemInfoProvider provider{nullptr};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("system.static");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 0u);
}

// ===========================================================================
// 假資料注入 → 列舉（多欄位：欄位數 = 實例數、欄位集 = 列舉實例）
// ===========================================================================
TEST(SystemInfoProvider, InjectedFieldsAreEnumerated) {
    MetricRegistry registry;
    SystemInfoProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    auto metric = registry.get("system.static");
    ASSERT_NE(metric, nullptr);

    // 「欄位數」= 實例數。
    EXPECT_EQ(metric->instance_count(), 5u);

    // 「欄位集」= 依序列舉實例（決定性 = 注入順序）。
    // 全程只用 E2-01 抽象 Metric / MetricInstance 介面，不觸及 sysinfo 具體型別。
    EXPECT_EQ(metric->instance(0).instance_id(), "os.name");
    EXPECT_EQ(metric->instance(0).label(), "OS Name");
    EXPECT_EQ(metric->instance(1).instance_id(), "os.version");
    EXPECT_EQ(metric->instance(2).instance_id(), "host.name");
    EXPECT_EQ(metric->instance(3).instance_id(), "cpu.model");
    EXPECT_EQ(metric->instance(4).instance_id(), "cpu.cores");
    EXPECT_EQ(metric->instance(4).label(), "CPU Cores");
}

// 純文字欄位承載文字值、number 為 0。
TEST(SystemInfoProvider, TextFieldCarriesText) {
    MetricRegistry registry;
    SystemInfoProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("system.static");
    ASSERT_NE(metric, nullptr);

    const auto v_os = metric->instance(0).value();
    EXPECT_TRUE(v_os.valid);
    EXPECT_DOUBLE_EQ(v_os.number, 0.0);
    ASSERT_TRUE(v_os.text.has_value());
    EXPECT_EQ(*v_os.text, "macOS");
}

// 數值欄位（核心數）同時承載數值維度與文字表述。
TEST(SystemInfoProvider, NumericFieldCarriesNumberAndText) {
    MetricRegistry registry;
    SystemInfoProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("system.static");
    ASSERT_NE(metric, nullptr);

    const auto v_cores = metric->instance(4).value();  // cpu.cores
    EXPECT_TRUE(v_cores.valid);
    EXPECT_DOUBLE_EQ(v_cores.number, 8.0);
    ASSERT_TRUE(v_cores.text.has_value());
    EXPECT_EQ(*v_cores.text, "8");
}

// find_instance 依欄位鍵尋得（E2-01 便利查詢，走抽象介面）。
TEST(SystemInfoProvider, FindInstanceByKey) {
    MetricRegistry registry;
    SystemInfoProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("system.static");
    ASSERT_NE(metric, nullptr);

    const auto* host = metric->find_instance("host.name");
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(host->label(), "Hostname");
    ASSERT_TRUE(host->value().text.has_value());
    EXPECT_EQ(*host->value().text, "dev-machine");
    EXPECT_EQ(metric->find_instance("does.not.exist"), nullptr);
}

// 範圍 = unbounded（靜態資訊無值域）。
TEST(SystemInfoProvider, MetricRangeIsUnbounded) {
    MetricRegistry registry;
    SystemInfoProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("system.static");
    ASSERT_NE(metric, nullptr);
    const auto r = metric->range();
    EXPECT_FALSE(r.has_min());
    EXPECT_FALSE(r.has_max());
    EXPECT_FALSE(r.is_bounded());
}

// 靜態資訊無時序歷史：history_capacity = 0。
TEST(SystemInfoProvider, FieldsHaveNoHistory) {
    MetricRegistry registry;
    SystemInfoProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);
    auto metric = registry.get("system.static");
    ASSERT_NE(metric, nullptr);

    const auto& h = metric->instance(0).history();
    EXPECT_EQ(h.capacity(), 0u);
    EXPECT_TRUE(h.empty());
}

// ===========================================================================
// null 後端注入 API：set_fields / add_field / clear / 建構子注入
// ===========================================================================
TEST(NullSysInfoSource, InjectionApi) {
    NullSysInfoSource src;
    EXPECT_EQ(src.size(), 0u);

    src.set_fields({SysInfoField{"a", "A", "1", 0.0}, SysInfoField{"b", "B", "2", 0.0}});
    EXPECT_EQ(src.size(), 2u);
    EXPECT_EQ(src.query().size(), 2u);

    src.add_field(SysInfoField{"c", "C", "3", 0.0});
    EXPECT_EQ(src.size(), 3u);

    src.clear();
    EXPECT_TRUE(src.empty());
    EXPECT_TRUE(src.query().empty());
}

TEST(NullSysInfoSource, ConstructorInjection) {
    NullSysInfoSource src{{SysInfoField{"os.name", "OS Name", "macOS", 0.0}}};
    ASSERT_EQ(src.size(), 1u);
    EXPECT_EQ(src.query()[0].text, "macOS");
}

// SysInfoField 相等性。
TEST(SysInfoField, Equality) {
    SysInfoField a{"cpu.cores", "CPU Cores", "8", 8.0};
    SysInfoField b{"cpu.cores", "CPU Cores", "8", 8.0};
    SysInfoField c{"cpu.cores", "CPU Cores", "4", 4.0};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ===========================================================================
// 消費者範式：新增指標 = 新增提供者、消費者只走 E2-01 registry
// ===========================================================================
TEST(SystemInfoProvider, ConsumerWalksViaAbstractContractOnly) {
    MetricRegistry registry;
    SystemInfoProvider provider{makeFakeSource()};
    ASSERT_EQ(registry.add_provider(provider), 1u);

    // 一個「掛件」風格的消費者：只認得 E2-01 的 Metric，數靜態欄位數量。
    std::size_t total = 0;
    for (const auto& m : registry.all()) {
        total += m->instance_count();   // 全程無 sysinfo 型別
    }
    EXPECT_EQ(total, 5u);
}

// 重複註冊保守拒絕（同 id 第二次掛上失敗、不覆寫）。
TEST(SystemInfoProvider, DuplicateRegistrationRejected) {
    MetricRegistry registry;
    SystemInfoProvider p1{makeFakeSource()};
    SystemInfoProvider p2{std::make_shared<NullSysInfoSource>()};

    EXPECT_EQ(registry.add_provider(p1), 1u);
    EXPECT_EQ(registry.add_provider(p2), 0u);   // 同 id "system.static" 被拒
    // 既有指標未被覆寫：仍為 p1 的五個欄位。
    auto metric = registry.get("system.static");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), 5u);
}

}  // namespace
