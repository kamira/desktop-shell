// E2-22 登錄檔讀取 — 測試（gtest）
//
// 覆蓋：讀取存在的鍵值、查無鍵、列舉子鍵、型別處理（型別錯明確回報）、null 來源行為、
// 值→MetricValue 對映、經 E2-01 provider 暴露（清單=列舉實例、數量=實例數）、
// 消費者只透過 E2-01 抽象介面走訪、重複註冊保守拒絕。
// 相位 1：只驗介面 + null/假來源行為，不含任何平台分支 / 真實登錄 API。
#include "registry_read.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "metric.hpp"

using ds::metrics::Metric;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRegistry;
using ds::metrics::MetricValue;
using ds::sysinfo::NullRegistrySource;
using ds::sysinfo::RegistryReaderProvider;
using ds::sysinfo::RegistrySource;
using ds::sysinfo::RegistryType;
using ds::sysinfo::RegistryValue;
using ds::sysinfo::registryValueToMetric;

namespace {

// 建一棵有代表性的假登錄樹。
std::shared_ptr<NullRegistrySource> makeFakeTree() {
    auto src = std::make_shared<NullRegistrySource>();
    src->set_value("HKLM/Software/App/Version", RegistryValue::makeString("1.2.3"));
    src->set_value("HKLM/Software/App/Build", RegistryValue::makeInteger(4096));
    src->set_value("HKLM/Software/App/Icon", RegistryValue::makeBinary({0x89, 0x50, 0x4E}));
    src->set_value("HKLM/Software/Other/Flag", RegistryValue::makeInteger(1));
    return src;
}

// ===========================================================================
// RegistryValue 型別處理（型別錯明確回報，不硬轉、不靜默）
// ===========================================================================
TEST(RegistryValue, StringAccessors) {
    RegistryValue v = RegistryValue::makeString("hello");
    EXPECT_EQ(v.type(), RegistryType::String);
    ASSERT_TRUE(v.as_string().has_value());
    EXPECT_EQ(v.as_string().value(), "hello");
    // 以錯型別讀 → nullopt（明確回報型別錯）。
    EXPECT_FALSE(v.as_integer().has_value());
    EXPECT_FALSE(v.as_binary().has_value());
}

TEST(RegistryValue, IntegerAccessors) {
    RegistryValue v = RegistryValue::makeInteger(-42);
    EXPECT_EQ(v.type(), RegistryType::Integer);
    ASSERT_TRUE(v.as_integer().has_value());
    EXPECT_EQ(v.as_integer().value(), static_cast<std::int64_t>(-42));
    EXPECT_FALSE(v.as_string().has_value());
    EXPECT_FALSE(v.as_binary().has_value());
}

TEST(RegistryValue, BinaryAccessors) {
    RegistryValue v = RegistryValue::makeBinary({0x01, 0x02, 0x03, 0x04});
    EXPECT_EQ(v.type(), RegistryType::Binary);
    ASSERT_TRUE(v.as_binary().has_value());
    EXPECT_EQ(v.as_binary().value().size(), static_cast<std::size_t>(4));
    EXPECT_FALSE(v.as_string().has_value());
    EXPECT_FALSE(v.as_integer().has_value());
}

TEST(RegistryValue, Equality) {
    EXPECT_EQ(RegistryValue::makeString("a"), RegistryValue::makeString("a"));
    EXPECT_NE(RegistryValue::makeString("a"), RegistryValue::makeString("b"));
    EXPECT_NE(RegistryValue::makeString("1"), RegistryValue::makeInteger(1));
    EXPECT_EQ(RegistryValue::makeInteger(7), RegistryValue::makeInteger(7));
    EXPECT_EQ(RegistryValue::makeBinary({1, 2}), RegistryValue::makeBinary({1, 2}));
    EXPECT_NE(RegistryValue::makeBinary({1, 2}), RegistryValue::makeBinary({1, 3}));
}

// ===========================================================================
// NullRegistrySource：讀取存在的鍵值 / 查無鍵
// ===========================================================================
TEST(NullRegistrySource, EmptyByDefault) {
    NullRegistrySource src;  // Mac / null 期預設：空樹
    EXPECT_TRUE(src.empty());
    EXPECT_EQ(src.size(), static_cast<std::size_t>(0));
    // 讀任何鍵皆回 nullopt（查無鍵，不回假值）。
    EXPECT_FALSE(src.read("HKLM/anything").has_value());
}

TEST(NullRegistrySource, ReadsExistingKey) {
    auto src = makeFakeTree();
    auto v = src->read("HKLM/Software/App/Version");
    ASSERT_TRUE(v.has_value());
    ASSERT_TRUE(v->as_string().has_value());
    EXPECT_EQ(v->as_string().value(), "1.2.3");

    auto b = src->read("HKLM/Software/App/Build");
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(b->as_integer().has_value());
    EXPECT_EQ(b->as_integer().value(), static_cast<std::int64_t>(4096));
}

TEST(NullRegistrySource, MissingKeyReturnsNullopt) {
    auto src = makeFakeTree();
    // 存在的父鍵下、不存在的葉 → 查無鍵。
    EXPECT_FALSE(src->read("HKLM/Software/App/DoesNotExist").has_value());
    // 完全不存在的路徑 → 查無鍵。
    EXPECT_FALSE(src->read("NOPE/xxx").has_value());
}

TEST(NullRegistrySource, SetRemoveClear) {
    NullRegistrySource src;
    src.set_value("a/b", RegistryValue::makeInteger(1));
    EXPECT_EQ(src.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(src.read("a/b").has_value());
    // 覆寫同路徑。
    src.set_value("a/b", RegistryValue::makeInteger(2));
    EXPECT_EQ(src.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(src.read("a/b")->as_integer().value(), static_cast<std::int64_t>(2));
    // 移除。
    EXPECT_TRUE(src.remove("a/b"));
    EXPECT_FALSE(src.remove("a/b"));
    EXPECT_TRUE(src.empty());
    // clear。
    src.set_value("x/y", RegistryValue::makeString("z"));
    src.clear();
    EXPECT_TRUE(src.empty());
}

// ===========================================================================
// enumerate：列舉子鍵（直屬子項、決定性順序）
// ===========================================================================
TEST(NullRegistrySource, EnumeratesImmediateChildren) {
    auto src = makeFakeTree();
    // HKLM/Software 底下的直屬子鍵：App、Other（字典序、去重）。
    auto kids = src->enumerate("HKLM/Software");
    ASSERT_EQ(kids.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(kids[0], "App");
    EXPECT_EQ(kids[1], "Other");
}

TEST(NullRegistrySource, EnumeratesLeafValueNames) {
    auto src = makeFakeTree();
    // HKLM/Software/App 底下的直屬子項（值名）：Build、Icon、Version（字典序）。
    auto kids = src->enumerate("HKLM/Software/App");
    ASSERT_EQ(kids.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(kids[0], "Build");
    EXPECT_EQ(kids[1], "Icon");
    EXPECT_EQ(kids[2], "Version");
}

TEST(NullRegistrySource, EnumeratesRoot) {
    auto src = makeFakeTree();
    auto roots = src->enumerate("");  // 頂層
    ASSERT_EQ(roots.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(roots[0], "HKLM");
}

TEST(NullRegistrySource, EnumerateUnknownKeyIsEmpty) {
    auto src = makeFakeTree();
    EXPECT_TRUE(src->enumerate("NOPE").empty());
    // 已是葉、其下無子項。
    EXPECT_TRUE(src->enumerate("HKLM/Software/App/Version").empty());
}

// ===========================================================================
// registryValueToMetric：值 → MetricValue 對映（含查無鍵）
// ===========================================================================
TEST(RegistryValueToMetric, MissingIsUnknown) {
    MetricValue mv = registryValueToMetric(std::nullopt);
    EXPECT_FALSE(mv.valid);  // 查無鍵 → 未知
}

TEST(RegistryValueToMetric, IntegerCarriesNumberAndText) {
    MetricValue mv = registryValueToMetric(RegistryValue::makeInteger(4096));
    EXPECT_TRUE(mv.valid);
    EXPECT_DOUBLE_EQ(mv.number, 4096.0);
    ASSERT_TRUE(mv.text.has_value());
    EXPECT_EQ(mv.text.value(), "4096");
}

TEST(RegistryValueToMetric, StringCarriesText) {
    MetricValue mv = registryValueToMetric(RegistryValue::makeString("1.2.3"));
    EXPECT_TRUE(mv.valid);
    ASSERT_TRUE(mv.text.has_value());
    EXPECT_EQ(mv.text.value(), "1.2.3");
}

TEST(RegistryValueToMetric, BinaryCarriesByteCountText) {
    MetricValue mv = registryValueToMetric(RegistryValue::makeBinary({1, 2, 3}));
    EXPECT_TRUE(mv.valid);
    ASSERT_TRUE(mv.text.has_value());
    EXPECT_EQ(mv.text.value(), "<binary:3 bytes>");
}

// ===========================================================================
// 提供者身分 + 經 E2-01 provider 暴露
// ===========================================================================
TEST(RegistryReaderProvider, ProviderIdIsStable) {
    RegistryReaderProvider p{std::make_shared<NullRegistrySource>(), {}};
    EXPECT_EQ(p.provider_id(), "sysinfo.registry");
    EXPECT_EQ(std::string(RegistryReaderProvider::kMetricId), "registry.values");
}

TEST(RegistryReaderProvider, IsAMetricProvider) {
    auto p = std::make_shared<RegistryReaderProvider>(
        std::make_shared<NullRegistrySource>(), std::vector<std::string>{});
    MetricProvider* base = p.get();  // 可上轉為 E2-01 抽象介面
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->provider_id(), "sysinfo.registry");
}

TEST(RegistryReaderProvider, RegistersMetricViaRegistry) {
    MetricRegistry registry;
    RegistryReaderProvider provider{makeFakeTree(),
                                    {"HKLM/Software/App/Version", "HKLM/Software/App/Build"}};
    const std::size_t added = registry.add_provider(provider);
    EXPECT_EQ(added, static_cast<std::size_t>(1));
    EXPECT_TRUE(registry.contains("registry.values"));
}

// 消費者範式：掛件風格消費者只走 E2-01 registry / Metric 介面，全程無 sysinfo 型別。
TEST(RegistryReaderProvider, ExposesReadValuesViaE201) {
    MetricRegistry registry;
    RegistryReaderProvider provider{
        makeFakeTree(),
        {"HKLM/Software/App/Version", "HKLM/Software/App/Build", "HKLM/Software/App/Icon"}};
    registry.add_provider(provider);

    auto metric = registry.get("registry.values");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->unit(), "");
    // 清單 = 列舉實例、數量 = 實例數。
    ASSERT_EQ(metric->instance_count(), static_cast<std::size_t>(3));

    // String 值：以文字承載。
    const auto* vi = metric->find_instance("HKLM/Software/App/Version");
    ASSERT_NE(vi, nullptr);
    EXPECT_TRUE(vi->value().valid);
    ASSERT_TRUE(vi->value().text.has_value());
    EXPECT_EQ(vi->value().text.value(), "1.2.3");

    // Integer 值：數值 + 文字。
    const auto* bi = metric->find_instance("HKLM/Software/App/Build");
    ASSERT_NE(bi, nullptr);
    EXPECT_TRUE(bi->value().valid);
    EXPECT_DOUBLE_EQ(bi->value().number, 4096.0);
}

// 查無鍵：經 provider 暴露為未知（valid==false），不靜默、不誤當 0。
TEST(RegistryReaderProvider, MissingKeyExposedAsUnknown) {
    MetricRegistry registry;
    RegistryReaderProvider provider{makeFakeTree(), {"HKLM/Software/App/Nope"}};
    registry.add_provider(provider);

    auto metric = registry.get("registry.values");
    ASSERT_NE(metric, nullptr);
    ASSERT_EQ(metric->instance_count(), static_cast<std::size_t>(1));
    const auto& inst = metric->instance(0);
    EXPECT_EQ(inst.instance_id(), "HKLM/Software/App/Nope");
    EXPECT_FALSE(inst.value().valid);  // 未知
}

// null 來源行為：source 為 null → 各選定鍵皆未知，仍保守掛上、不崩。
TEST(RegistryReaderProvider, NullSourceIsConservative) {
    MetricRegistry registry;
    RegistryReaderProvider provider{nullptr, {"a/b", "c/d"}};
    const std::size_t added = registry.add_provider(provider);
    EXPECT_EQ(added, static_cast<std::size_t>(1));

    auto metric = registry.get("registry.values");
    ASSERT_NE(metric, nullptr);
    ASSERT_EQ(metric->instance_count(), static_cast<std::size_t>(2));
    for (std::size_t i = 0; i < metric->instance_count(); ++i) {
        EXPECT_FALSE(metric->instance(i).value().valid);  // 全未知
    }
}

// 無選定鍵：仍保守掛上一個空指標（instance_count()==0），不崩。
TEST(RegistryReaderProvider, NoKeysStillRegistersEmptyMetric) {
    MetricRegistry registry;
    RegistryReaderProvider provider{makeFakeTree(), {}};
    const std::size_t added = registry.add_provider(provider);
    EXPECT_EQ(added, static_cast<std::size_t>(1));
    auto metric = registry.get("registry.values");
    ASSERT_NE(metric, nullptr);
    EXPECT_EQ(metric->instance_count(), static_cast<std::size_t>(0));
}

// 重複註冊：同 id 第二次掛上被註冊表保守拒絕、不覆寫既有。
TEST(RegistryReaderProvider, DuplicateRegistrationRejected) {
    MetricRegistry registry;
    RegistryReaderProvider p1{makeFakeTree(), {"HKLM/Software/App/Version"}};
    RegistryReaderProvider p2{makeFakeTree(), {"HKLM/Software/App/Build"}};
    EXPECT_EQ(registry.add_provider(p1), static_cast<std::size_t>(1));
    EXPECT_EQ(registry.add_provider(p2), static_cast<std::size_t>(0));  // 重複 id 拒絕
    EXPECT_EQ(registry.size(), static_cast<std::size_t>(1));
}

}  // namespace
