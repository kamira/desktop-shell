// E2-01 統一指標介面 — 契約測試（gtest）
//
// 覆蓋：值/單位/範圍要素、歷史環狀行為、多實例列舉、註冊/查詢/移除、
// 未知指標保守回應、提供者掛載、以及**契約穩定性**（消費者只透過抽象介面走訪，
// 全程不觸及任何具體感測器型別）。
// 相位 1：只驗介面 + 記憶體內（null 期）行為，不含任何平台分支。
#include "metric.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using ds::metrics::InMemoryMetric;
using ds::metrics::InMemoryMetricInstance;
using ds::metrics::Metric;
using ds::metrics::MetricHistory;
using ds::metrics::MetricInstance;
using ds::metrics::MetricProvider;
using ds::metrics::MetricRange;
using ds::metrics::MetricRegistry;
using ds::metrics::MetricValue;

namespace {

// ===========================================================================
// MetricValue（值 / 多型值最小形）
// ===========================================================================
TEST(MetricValue, DefaultIsUnknown) {
    MetricValue v;
    EXPECT_FALSE(v.valid);
    EXPECT_FALSE(v.text.has_value());
    EXPECT_EQ(v, MetricValue::unknown());
}

TEST(MetricValue, NumericFactory) {
    MetricValue v = MetricValue::of(42.5);
    EXPECT_TRUE(v.valid);
    EXPECT_DOUBLE_EQ(v.number, 42.5);
    EXPECT_FALSE(v.text.has_value());
}

TEST(MetricValue, TextFactoryCarriesBothDimensions) {
    MetricValue v = MetricValue::of(82.0, "82°C");
    EXPECT_TRUE(v.valid);
    EXPECT_DOUBLE_EQ(v.number, 82.0);
    ASSERT_TRUE(v.text.has_value());
    EXPECT_EQ(*v.text, "82°C");
}

// ===========================================================================
// MetricRange（範圍 / 正規化）
// ===========================================================================
TEST(MetricRange, UnboundedNormalizesToNullopt) {
    MetricRange r = MetricRange::unbounded();
    EXPECT_FALSE(r.has_min());
    EXPECT_FALSE(r.has_max());
    EXPECT_FALSE(r.is_bounded());
    EXPECT_FALSE(r.normalized(100.0).has_value());
}

TEST(MetricRange, HalfBoundedClampsOneSideOnly) {
    MetricRange lo = MetricRange::at_least(0.0);
    EXPECT_TRUE(lo.has_min());
    EXPECT_FALSE(lo.has_max());
    EXPECT_DOUBLE_EQ(lo.clamp(-5.0), 0.0);
    EXPECT_DOUBLE_EQ(lo.clamp(999.0), 999.0);   // 上無界，不夾
    EXPECT_FALSE(lo.normalized(50.0).has_value());  // 非有界不可正規化
}

TEST(MetricRange, BoundedNormalizesAndClamps) {
    MetricRange r = MetricRange::bounded(0.0, 100.0);
    EXPECT_TRUE(r.is_bounded());
    ASSERT_TRUE(r.normalized(25.0).has_value());
    EXPECT_DOUBLE_EQ(*r.normalized(25.0), 0.25);
    // 超界值夾到 [0,1]
    EXPECT_DOUBLE_EQ(*r.normalized(-10.0), 0.0);
    EXPECT_DOUBLE_EQ(*r.normalized(150.0), 1.0);
    // clamp 亦然
    EXPECT_DOUBLE_EQ(r.clamp(-10.0), 0.0);
    EXPECT_DOUBLE_EQ(r.clamp(150.0), 100.0);
}

TEST(MetricRange, DegenerateRangeNotNormalizable) {
    EXPECT_FALSE(MetricRange::bounded(5.0, 5.0).normalized(5.0).has_value());  // max==min
    EXPECT_FALSE(MetricRange::bounded(10.0, 0.0).normalized(5.0).has_value()); // 反向
}

// ===========================================================================
// MetricHistory（歷史環狀行為）
// ===========================================================================
TEST(MetricHistory, EmptyQueriesThrow) {
    MetricHistory h(4);
    EXPECT_TRUE(h.empty());
    EXPECT_EQ(h.size(), 0u);
    EXPECT_EQ(h.capacity(), 4u);
    EXPECT_THROW(h.latest(), std::out_of_range);
    EXPECT_THROW(h.at(0), std::out_of_range);
}

TEST(MetricHistory, FillsInOrderOldestToNewest) {
    MetricHistory h(4);
    h.push(1.0);
    h.push(2.0);
    h.push(3.0);
    EXPECT_EQ(h.size(), 3u);
    EXPECT_FALSE(h.full());
    EXPECT_DOUBLE_EQ(h.at(0), 1.0);   // 最舊
    EXPECT_DOUBLE_EQ(h.at(2), 3.0);   // 最新
    EXPECT_DOUBLE_EQ(h.latest(), 3.0);
    EXPECT_EQ(h.to_vector(), (std::vector<double>{1.0, 2.0, 3.0}));
}

TEST(MetricHistory, RingOverwritesOldestWhenFull) {
    MetricHistory h(3);
    h.push(1.0);
    h.push(2.0);
    h.push(3.0);
    EXPECT_TRUE(h.full());
    EXPECT_EQ(h.to_vector(), (std::vector<double>{1.0, 2.0, 3.0}));
    // 覆蓋最舊：1 被 4 頂掉
    h.push(4.0);
    EXPECT_EQ(h.size(), 3u);
    EXPECT_EQ(h.to_vector(), (std::vector<double>{2.0, 3.0, 4.0}));
    EXPECT_DOUBLE_EQ(h.at(0), 2.0);
    EXPECT_DOUBLE_EQ(h.latest(), 4.0);
    // 再覆蓋一圈
    h.push(5.0);
    h.push(6.0);
    EXPECT_EQ(h.to_vector(), (std::vector<double>{4.0, 5.0, 6.0}));
}

TEST(MetricHistory, OutOfRangeIndexThrowsAtBoundary) {
    MetricHistory h(3);
    h.push(1.0);
    h.push(2.0);
    EXPECT_NO_THROW(h.at(1));
    EXPECT_THROW(h.at(2), std::out_of_range);  // size==2，index 2 越界
}

TEST(MetricHistory, ClearResetsButKeepsCapacity) {
    MetricHistory h(3);
    h.push(1.0);
    h.push(2.0);
    h.clear();
    EXPECT_TRUE(h.empty());
    EXPECT_EQ(h.capacity(), 3u);
    h.push(9.0);
    EXPECT_EQ(h.to_vector(), (std::vector<double>{9.0}));
}

TEST(MetricHistory, ZeroCapacityIsNoOp) {
    MetricHistory h(0);
    EXPECT_EQ(h.capacity(), 0u);
    h.push(1.0);
    h.push(2.0);
    EXPECT_TRUE(h.empty());
    EXPECT_EQ(h.size(), 0u);
    EXPECT_FALSE(h.full());
    EXPECT_THROW(h.latest(), std::out_of_range);
}

// ===========================================================================
// InMemoryMetricInstance（值 + 歷史連動）
// ===========================================================================
TEST(InMemoryMetricInstance, StartsUnknownWithEmptyHistory) {
    InMemoryMetricInstance inst("cpu0", "Core 0", 8);
    EXPECT_EQ(inst.instance_id(), "cpu0");
    EXPECT_EQ(inst.label(), "Core 0");
    EXPECT_FALSE(inst.value().valid);
    EXPECT_TRUE(inst.history().empty());
}

TEST(InMemoryMetricInstance, UpdatePushesValidValuesToHistory) {
    InMemoryMetricInstance inst("cpu0", "Core 0", 8);
    inst.push(10.0);
    inst.push(20.0);
    EXPECT_TRUE(inst.value().valid);
    EXPECT_DOUBLE_EQ(inst.value().number, 20.0);
    EXPECT_EQ(inst.history().to_vector(), (std::vector<double>{10.0, 20.0}));
}

TEST(InMemoryMetricInstance, UnknownUpdateDoesNotPolluteHistory) {
    InMemoryMetricInstance inst("cpu0", "Core 0", 8);
    inst.push(10.0);
    inst.update(MetricValue::unknown());  // 感測失敗：值標未知，但歷史不加點
    EXPECT_FALSE(inst.value().valid);
    EXPECT_EQ(inst.history().to_vector(), (std::vector<double>{10.0}));
}

TEST(InMemoryMetricInstance, SetValueDoesNotTouchHistory) {
    InMemoryMetricInstance inst("cpu0", "Core 0", 8);
    inst.push(10.0);
    inst.set_value(MetricValue::of(99.0));  // 只改當前值
    EXPECT_DOUBLE_EQ(inst.value().number, 99.0);
    EXPECT_EQ(inst.history().to_vector(), (std::vector<double>{10.0}));
}

// ===========================================================================
// InMemoryMetric（單位 / 範圍 / 單值 + 多實例列舉）
// ===========================================================================
TEST(InMemoryMetric, SingleInstanceCarriesNameUnitRange) {
    InMemoryMetric m("mem.used", "Memory Used", "MB", MetricRange::bounded(0.0, 16384.0));
    m.add_instance(Metric::kSingleInstanceId, "Total", 16).push(8192.0);

    EXPECT_EQ(m.id(), "mem.used");
    EXPECT_EQ(m.name(), "Memory Used");
    EXPECT_EQ(m.unit(), "MB");
    EXPECT_TRUE(m.range().is_bounded());
    EXPECT_TRUE(m.is_single());
    ASSERT_EQ(m.instance_count(), 1u);
    EXPECT_DOUBLE_EQ(m.single().value().number, 8192.0);
    ASSERT_TRUE(m.range().normalized(8192.0).has_value());
    EXPECT_DOUBLE_EQ(*m.range().normalized(8192.0), 0.5);
}

TEST(InMemoryMetric, MultiInstanceEnumeration) {
    // 每核心 CPU：四個實例，各有獨立值與歷史。
    InMemoryMetric cpu("cpu.core.usage", "CPU Usage (per core)", "%",
                       MetricRange::bounded(0.0, 100.0));
    for (int i = 0; i < 4; ++i) {
        auto& inst = cpu.add_instance("core" + std::to_string(i),
                                      "Core " + std::to_string(i), 32);
        inst.push(static_cast<double>(i) * 10.0);
    }
    ASSERT_EQ(cpu.instance_count(), 4u);
    EXPECT_FALSE(cpu.is_single());

    // 列舉走訪，順序穩定。
    for (std::size_t i = 0; i < cpu.instance_count(); ++i) {
        const MetricInstance& inst = cpu.instance(i);
        EXPECT_EQ(inst.instance_id(), "core" + std::to_string(i));
        EXPECT_DOUBLE_EQ(inst.value().number, static_cast<double>(i) * 10.0);
    }

    // 各實例歷史互相獨立。
    EXPECT_EQ(cpu.instance(0).history().to_vector(), (std::vector<double>{0.0}));
    EXPECT_EQ(cpu.instance(3).history().to_vector(), (std::vector<double>{30.0}));

    // single() 對多實例指標擲例外（防止誤用）。
    EXPECT_THROW(cpu.single(), std::out_of_range);
}

TEST(InMemoryMetric, FindInstanceById) {
    InMemoryMetric cpu("cpu.core.usage", "CPU", "%", MetricRange::bounded(0.0, 100.0));
    cpu.add_instance("core0", "Core 0", 8);
    cpu.add_instance("core1", "Core 1", 8);
    const MetricInstance* found = cpu.find_instance("core1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->label(), "Core 1");
    EXPECT_EQ(cpu.find_instance("core9"), nullptr);  // 未知實例
}

TEST(InMemoryMetric, InstanceIndexOutOfRangeThrows) {
    InMemoryMetric m("x", "X", "", MetricRange::unbounded());
    m.add_instance("only", "Only", 4);
    EXPECT_NO_THROW(m.instance(0));
    EXPECT_THROW(m.instance(1), std::out_of_range);
}

TEST(InMemoryMetric, AddInstanceReferencesStayValidAsMoreAdded) {
    // 契約：add_instance 回傳的參照在後續新增後仍有效（unique_ptr 持有）。
    InMemoryMetric m("x", "X", "", MetricRange::unbounded());
    auto& first = m.add_instance("a", "A", 4);
    m.add_instance("b", "B", 4);
    m.add_instance("c", "C", 4);
    first.push(7.0);  // 若參照失效此處即壞
    EXPECT_DOUBLE_EQ(m.instance(0).value().number, 7.0);
}

// ===========================================================================
// MetricRegistry（註冊 / 查詢 / 移除 / 未知指標）
// ===========================================================================
std::shared_ptr<InMemoryMetric> make_metric(const std::string& id) {
    auto m = std::make_shared<InMemoryMetric>(id, id, "%", MetricRange::bounded(0.0, 100.0));
    m->add_instance(Metric::kSingleInstanceId, "Total", 8);
    return m;
}

TEST(MetricRegistry, RegisterAndGet) {
    MetricRegistry reg;
    EXPECT_TRUE(reg.empty());
    EXPECT_TRUE(reg.register_metric(make_metric("cpu.usage")));
    EXPECT_EQ(reg.size(), 1u);
    EXPECT_TRUE(reg.contains("cpu.usage"));
    auto m = reg.get("cpu.usage");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->id(), "cpu.usage");
}

TEST(MetricRegistry, UnknownMetricIsConservativelyAbsent) {
    MetricRegistry reg;
    reg.register_metric(make_metric("cpu.usage"));
    EXPECT_FALSE(reg.contains("gpu.usage"));
    EXPECT_EQ(reg.get("gpu.usage"), nullptr);   // 未知：回 nullptr，不擲例外
    EXPECT_FALSE(reg.unregister("gpu.usage"));  // 移除未知：回 false
}

TEST(MetricRegistry, RejectsNullEmptyIdAndDuplicate) {
    MetricRegistry reg;
    EXPECT_FALSE(reg.register_metric(nullptr));  // null
    auto empty_id = std::make_shared<InMemoryMetric>("", "n", "", MetricRange::unbounded());
    EXPECT_FALSE(reg.register_metric(empty_id));  // 空 id
    EXPECT_EQ(reg.size(), 0u);

    EXPECT_TRUE(reg.register_metric(make_metric("cpu.usage")));
    // 重複 id：保守拒絕，不覆寫既有。
    auto dup = make_metric("cpu.usage");
    EXPECT_FALSE(reg.register_metric(dup));
    EXPECT_EQ(reg.size(), 1u);
    EXPECT_NE(reg.get("cpu.usage"), dup);  // 既有者未被替換
}

TEST(MetricRegistry, EnumerationIsRegistrationOrder) {
    MetricRegistry reg;
    reg.register_metric(make_metric("a"));
    reg.register_metric(make_metric("b"));
    reg.register_metric(make_metric("c"));
    EXPECT_EQ(reg.ids(), (std::vector<std::string>{"a", "b", "c"}));
    ASSERT_EQ(reg.all().size(), 3u);
    EXPECT_EQ(reg.all()[1]->id(), "b");
}

TEST(MetricRegistry, UnregisterKeepsRemainingQueryable) {
    MetricRegistry reg;
    reg.register_metric(make_metric("a"));
    reg.register_metric(make_metric("b"));
    reg.register_metric(make_metric("c"));
    EXPECT_TRUE(reg.unregister("b"));
    EXPECT_EQ(reg.size(), 2u);
    EXPECT_FALSE(reg.contains("b"));
    // 其餘仍可查、索引未錯位。
    EXPECT_EQ(reg.ids(), (std::vector<std::string>{"a", "c"}));
    ASSERT_NE(reg.get("a"), nullptr);
    ASSERT_NE(reg.get("c"), nullptr);
    EXPECT_EQ(reg.get("c")->id(), "c");
    // 移除後可重新註冊同 id。
    EXPECT_TRUE(reg.register_metric(make_metric("b")));
    EXPECT_EQ(reg.ids(), (std::vector<std::string>{"a", "c", "b"}));
}

// ===========================================================================
// MetricProvider（提供者掛載一個或多個指標）
// ===========================================================================
// 假提供者：一個提供者掛上多個指標——證明「新增指標 = 新增提供者」機制。
class FakeMultiProvider : public MetricProvider {
public:
    std::string provider_id() const override { return "sysinfo.fake"; }
    void register_metrics(MetricRegistry& reg) override {
        reg.register_metric(make_metric("cpu.usage"));
        reg.register_metric(make_metric("gpu.usage"));
    }
};

TEST(MetricProvider, AddProviderHangsAllItsMetrics) {
    MetricRegistry reg;
    FakeMultiProvider provider;
    EXPECT_EQ(provider.provider_id(), "sysinfo.fake");
    std::size_t added = reg.add_provider(provider);
    EXPECT_EQ(added, 2u);
    EXPECT_TRUE(reg.contains("cpu.usage"));
    EXPECT_TRUE(reg.contains("gpu.usage"));
}

TEST(MetricProvider, AddProviderReportsOnlyNewlyAdded) {
    MetricRegistry reg;
    reg.register_metric(make_metric("cpu.usage"));  // 先佔一個 id
    FakeMultiProvider provider;
    // provider 想加 cpu.usage(重複被拒) + gpu.usage(成功) → 只 +1。
    EXPECT_EQ(reg.add_provider(provider), 1u);
    EXPECT_EQ(reg.size(), 2u);
}

// ===========================================================================
// 契約穩定性：消費者只透過抽象介面走訪，不觸及任何具體感測器型別
// ===========================================================================
// 此函式僅認得 Metric / MetricInstance / MetricRegistry 抽象契約，
// 完全不認得 InMemoryMetric——模擬真實 widget。若契約形狀不足，此函式無法編譯。
std::size_t generic_consumer_total_points(const MetricRegistry& reg) {
    std::size_t total = 0;
    for (const std::shared_ptr<Metric>& m : reg.all()) {
        (void)m->name();
        (void)m->unit();
        const MetricRange r = m->range();
        for (std::size_t i = 0; i < m->instance_count(); ++i) {
            const MetricInstance& inst = m->instance(i);
            const MetricValue v = inst.value();
            if (v.valid) (void)r.normalized(v.number);  // 只靠契約即可正規化
            total += inst.history().size();
        }
    }
    return total;
}

TEST(ContractStability, ConsumerWalksEverythingViaInterfacesOnly) {
    MetricRegistry reg;
    FakeMultiProvider provider;
    reg.add_provider(provider);
    // 各推幾個歷史點。
    for (auto& m : reg.all()) {
        auto* im = dynamic_cast<InMemoryMetric*>(m.get());
        ASSERT_NE(im, nullptr);
        auto& inst = const_cast<InMemoryMetricInstance&>(
            *dynamic_cast<const InMemoryMetricInstance*>(&im->instance(0)));
        inst.push(30.0);
        inst.push(60.0);
    }
    EXPECT_EQ(generic_consumer_total_points(reg), 4u);  // 2 指標 × 2 點
}

// 契約的靜態性質：介面為抽象、具體實作確為其子型別。API 形狀錯了此處即紅。
TEST(ContractStability, InterfaceShapeInvariants) {
    static_assert(std::is_abstract<Metric>::value, "Metric 必須是抽象契約");
    static_assert(std::is_abstract<MetricInstance>::value, "MetricInstance 必須是抽象契約");
    static_assert(std::is_abstract<MetricProvider>::value, "MetricProvider 必須是抽象契約");
    static_assert(std::is_base_of<Metric, InMemoryMetric>::value, "InMemoryMetric 應實作 Metric");
    static_assert(std::is_base_of<MetricInstance, InMemoryMetricInstance>::value,
                  "InMemoryMetricInstance 應實作 MetricInstance");
    // 註冊表不可複製（避免 shared_ptr 集合誤複製），但可移動。
    static_assert(!std::is_copy_constructible<MetricRegistry>::value,
                  "MetricRegistry 不應可複製");
    static_assert(std::is_move_constructible<MetricRegistry>::value,
                  "MetricRegistry 應可移動");
    SUCCEED();
}

}  // namespace
