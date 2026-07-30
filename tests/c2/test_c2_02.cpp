// tests/c2/test_c2_02.cpp — C2-02 系統狀態 widget（gtest）
//
// 涵蓋：C1-01 基底組裝、configure()（多指標選擇 + E7-03 vars 段落 + E7-05 min/max 靜態公式 /
// transform 公式語法驗證、空選擇、各類無效具名設定、vars 非 Map 之 ResolveError、min 公式求值
// 失敗之 FormulaError、全有或全無不改既有設定）、sync_demands()/release_demands()（E2-02 依 tier
// 登記 / 撤銷取樣需求，冪等）、refresh()（未 configure 即刷新、經注入 E2-01 MetricRegistry 讀值、
// 指標不存在 / 值無效降級 NFR-03、transform 公式動態單位換算）、render()（未 refresh 即呈現、
// Bar/Gauge/Progress 經 E4-03 組裝渲染描述、Text 種類不掛元素、E4-01 文字排版、不可用指標的
// 佔位呈現）。
#include "system_status_widget.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using ds::elements::ElementKind;
using ds::format::Value;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::metrics::InMemoryMetric;
using ds::metrics::MetricRange;
using ds::metrics::MetricRegistry;
using ds::metrics::MetricValue;
using ds::metrics::SamplingScheduler;
using ds::metrics::SamplingTier;
using ds::profiles::SkinState;
using ds::render::FixedFontMetrics;
using ds::widgets::MetricElementKind;
using ds::widgets::SystemStatusStatus;
using ds::widgets::SystemStatusWidget;

namespace {

// 測試固定件：以 defaults() 能力矩陣建構後端 / 圖層堆疊（本 widget 不直接測試 load_skin，
// 只驗證 C1-01 基底已正確組裝）。
struct Fixture {
    NullKernelBackend backend{CapabilityMatrix::defaults()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics{6.0, 14.0};
};

Value metric_entry(std::vector<Value::Member> fields) {
    return Value::map(std::move(fields));
}

}  // namespace

// -----------------------------------------------------------------------------
// 建構 / C1-01 基底組裝
// -----------------------------------------------------------------------------


TEST(SystemStatusWidget, ConstructedHasAssembledBaseAndDefaults) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.main", f.backend, f.layers, f.metrics};

    EXPECT_EQ(widget.id(), std::string("widget.sysstat.main"));
    EXPECT_EQ(widget.base().id(), std::string("widget.sysstat.main"));
    EXPECT_EQ(widget.base().state(), SkinState::Unloaded);

    EXPECT_FALSE(widget.is_configured());
    EXPECT_TRUE(widget.visible_metrics().empty());
    EXPECT_FALSE(widget.has_refreshed());
    EXPECT_TRUE(widget.render_model().entries.empty());
}

// -----------------------------------------------------------------------------
// configure()：多指標選擇（E7-03 vars 段落 + E7-05 公式）
// -----------------------------------------------------------------------------

TEST(SystemStatusWidget, ConfigureMultipleMetricsWithVarsAndFormulas) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.a", f.backend, f.layers, f.metrics};

    Value def = Value::map({
        {"vars", Value::map({{"gb", Value::number(1073741824)}})},
        {"metrics",
         Value::list({
             metric_entry({
                 {"id", Value::string("cpu.usage")},
                 {"label", Value::string("CPU")},
                 {"kind", Value::string("gauge")},
                 {"min", Value::number(0)},
                 {"max", Value::number(100)},
                 {"tier", Value::string("high")},
             }),
             metric_entry({
                 {"id", Value::string("mem.used")},
                 {"kind", Value::string("bar")},
                 {"transform", Value::string("= value / gb")},
                 {"tier", Value::string("low")},
             }),
         })},
    });

    EXPECT_EQ(widget.configure(def), SystemStatusStatus::Ok);
    EXPECT_TRUE(widget.is_configured());
    ASSERT_EQ(widget.visible_metrics().size(), 2u);

    const auto& cpu = widget.visible_metrics()[0];
    EXPECT_EQ(cpu.metric_id, std::string("cpu.usage"));
    EXPECT_EQ(cpu.label, std::string("CPU"));
    EXPECT_EQ(cpu.kind, MetricElementKind::Gauge);
    EXPECT_TRUE(cpu.has_range_override);
    EXPECT_DOUBLE_EQ(cpu.range_min, 0.0);
    EXPECT_DOUBLE_EQ(cpu.range_max, 100.0);
    EXPECT_EQ(cpu.tier, SamplingTier::High);

    const auto& mem = widget.visible_metrics()[1];
    EXPECT_EQ(mem.metric_id, std::string("mem.used"));
    EXPECT_EQ(mem.kind, MetricElementKind::Bar);
    EXPECT_FALSE(mem.has_range_override);
    EXPECT_EQ(mem.transform, std::string("= value / gb"));
    EXPECT_EQ(mem.tier, SamplingTier::Low);
}

TEST(SystemStatusWidget, ConfigureEmptySelectionIsValid) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.b", f.backend, f.layers, f.metrics};

    Value def = Value::map({{"metrics", Value::list({})}});
    EXPECT_EQ(widget.configure(def), SystemStatusStatus::Ok);
    EXPECT_TRUE(widget.is_configured());
    EXPECT_TRUE(widget.visible_metrics().empty());
}

TEST(SystemStatusWidget, ConfigureInvalidNonMapRoot) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.c", f.backend, f.layers, f.metrics};

    EXPECT_EQ(widget.configure(Value::string("nope")), SystemStatusStatus::Invalid);
    EXPECT_FALSE(widget.is_configured());
}

TEST(SystemStatusWidget, ConfigureInvalidMissingMetricsField) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.d", f.backend, f.layers, f.metrics};

    EXPECT_EQ(widget.configure(Value::map({})), SystemStatusStatus::Invalid);
    EXPECT_FALSE(widget.is_configured());
}

TEST(SystemStatusWidget, ConfigureInvalidEntryNotMap) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.e", f.backend, f.layers, f.metrics};

    Value def = Value::map({{"metrics", Value::list({Value::string("not-a-map")})}});
    EXPECT_EQ(widget.configure(def), SystemStatusStatus::Invalid);
}

TEST(SystemStatusWidget, ConfigureInvalidMissingId) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.f", f.backend, f.layers, f.metrics};

    Value def = Value::map({{"metrics", Value::list({metric_entry({})})}});
    EXPECT_EQ(widget.configure(def), SystemStatusStatus::Invalid);
}

TEST(SystemStatusWidget, ConfigureInvalidUnknownKind) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.g", f.backend, f.layers, f.metrics};

    Value def = Value::map({{"metrics",
                             Value::list({metric_entry({{"id", Value::string("cpu.usage")},
                                                        {"kind", Value::string("circle")}})})}});
    EXPECT_EQ(widget.configure(def), SystemStatusStatus::Invalid);
}

TEST(SystemStatusWidget, ConfigureInvalidUnknownTier) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.h", f.backend, f.layers, f.metrics};

    Value def = Value::map({{"metrics",
                             Value::list({metric_entry({{"id", Value::string("cpu.usage")},
                                                        {"tier", Value::string("urgent")}})})}});
    EXPECT_EQ(widget.configure(def), SystemStatusStatus::Invalid);
}

TEST(SystemStatusWidget, ConfigureInvalidMinWithoutMax) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.i", f.backend, f.layers, f.metrics};

    Value def = Value::map({{"metrics",
                             Value::list({metric_entry({{"id", Value::string("cpu.usage")},
                                                        {"min", Value::number(0)}})})}});
    EXPECT_EQ(widget.configure(def), SystemStatusStatus::Invalid);
}

TEST(SystemStatusWidget, ConfigureInvalidDegenerateRange) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.i2", f.backend, f.layers, f.metrics};

    Value def = Value::map({{"metrics",
                             Value::list({metric_entry({{"id", Value::string("cpu.usage")},
                                                        {"min", Value::number(10)},
                                                        {"max", Value::number(10)}})})}});
    EXPECT_EQ(widget.configure(def), SystemStatusStatus::Invalid);
}

TEST(SystemStatusWidget, ConfigureInvalidTransformNotFormulaSyntax) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.j", f.backend, f.layers, f.metrics};

    Value def = Value::map({{"metrics",
                             Value::list({metric_entry({{"id", Value::string("cpu.usage")},
                                                        {"transform", Value::string("value / 2")}})})}});
    EXPECT_EQ(widget.configure(def), SystemStatusStatus::Invalid);
}

TEST(SystemStatusWidget, ConfigureResolveErrorWhenVarsSectionNotMap) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.k", f.backend, f.layers, f.metrics};

    Value def = Value::map({{"vars", Value::string("nope")}, {"metrics", Value::list({})}});
    EXPECT_EQ(widget.configure(def), SystemStatusStatus::ResolveError);
    EXPECT_FALSE(widget.is_configured());
}

TEST(SystemStatusWidget, ConfigureFormulaErrorOnUndefinedVariableInMin) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.l", f.backend, f.layers, f.metrics};

    Value def = Value::map({{"metrics",
                             Value::list({metric_entry({{"id", Value::string("cpu.usage")},
                                                        {"min", Value::string("= undefined_var")},
                                                        {"max", Value::number(10)}})})}});
    EXPECT_EQ(widget.configure(def), SystemStatusStatus::FormulaError);
    EXPECT_FALSE(widget.is_configured());
}

TEST(SystemStatusWidget, ConfigureFailureKeepsPreviousSelection) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.m", f.backend, f.layers, f.metrics};

    Value good = Value::map(
        {{"metrics", Value::list({metric_entry({{"id", Value::string("cpu.usage")}})})}});
    ASSERT_EQ(widget.configure(good), SystemStatusStatus::Ok);
    ASSERT_EQ(widget.visible_metrics().size(), 1u);

    EXPECT_EQ(widget.configure(Value::string("bad")), SystemStatusStatus::Invalid);
    // 全有或全無：失敗不改既有選擇。
    ASSERT_EQ(widget.visible_metrics().size(), 1u);
    EXPECT_EQ(widget.visible_metrics()[0].metric_id, std::string("cpu.usage"));
}

// -----------------------------------------------------------------------------
// sync_demands() / release_demands()（E2-02 除頻登記）
// -----------------------------------------------------------------------------

TEST(SystemStatusWidget, SyncDemandsRegistersPerTierAndReleaseUnregisters) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.n", f.backend, f.layers, f.metrics};

    Value def = Value::map({{"metrics",
                             Value::list({
                                 metric_entry({{"id", Value::string("cpu.usage")},
                                              {"tier", Value::string("high")}}),
                                 metric_entry({{"id", Value::string("disk.free")},
                                              {"tier", Value::string("low")}}),
                             })}});
    ASSERT_EQ(widget.configure(def), SystemStatusStatus::Ok);

    SamplingScheduler scheduler;
    widget.sync_demands(scheduler);

    EXPECT_TRUE(scheduler.tracks("cpu.usage"));
    ASSERT_TRUE(scheduler.effective_tier("cpu.usage").has_value());
    EXPECT_EQ(*scheduler.effective_tier("cpu.usage"), SamplingTier::High);

    EXPECT_TRUE(scheduler.tracks("disk.free"));
    ASSERT_TRUE(scheduler.effective_tier("disk.free").has_value());
    EXPECT_EQ(*scheduler.effective_tier("disk.free"), SamplingTier::Low);

    // 冪等：再次 sync 不重複疊加需求。
    widget.sync_demands(scheduler);
    EXPECT_EQ(scheduler.demand_count("cpu.usage"), 1u);

    widget.release_demands();
    EXPECT_FALSE(scheduler.tracks("cpu.usage"));
    EXPECT_FALSE(scheduler.tracks("disk.free"));
}

TEST(SystemStatusWidget, SyncDemandsNoOpWhenSelectionEmpty) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.o", f.backend, f.layers, f.metrics};
    ASSERT_EQ(widget.configure(Value::map({{"metrics", Value::list({})}})),
              SystemStatusStatus::Ok);

    SamplingScheduler scheduler;
    widget.sync_demands(scheduler);
    EXPECT_EQ(scheduler.metric_count(), 0u);
}

// -----------------------------------------------------------------------------
// refresh()（注入 E2-01 MetricRegistry；NFR-03 優雅降級）
// -----------------------------------------------------------------------------

TEST(SystemStatusWidget, RefreshInvalidBeforeConfigure) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.p", f.backend, f.layers, f.metrics};

    MetricRegistry registry;
    EXPECT_EQ(widget.refresh(registry), SystemStatusStatus::Invalid);
    EXPECT_FALSE(widget.has_refreshed());
}

TEST(SystemStatusWidget, RefreshReadsAvailableMetric) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.q", f.backend, f.layers, f.metrics};

    Value def = Value::map(
        {{"metrics", Value::list({metric_entry({{"id", Value::string("cpu.usage")},
                                                {"kind", Value::string("bar")},
                                                {"min", Value::number(0)},
                                                {"max", Value::number(100)}})})}});
    ASSERT_EQ(widget.configure(def), SystemStatusStatus::Ok);

    auto cpu = std::make_shared<InMemoryMetric>("cpu.usage", "CPU Usage", "%",
                                                MetricRange::bounded(0.0, 100.0));
    cpu->add_instance("", "CPU", 4).push(42.5);
    MetricRegistry registry;
    ASSERT_TRUE(registry.register_metric(cpu));

    EXPECT_EQ(widget.refresh(registry), SystemStatusStatus::Ok);
    EXPECT_TRUE(widget.has_refreshed());

    ASSERT_EQ(widget.render(), SystemStatusStatus::Ok);
    ASSERT_EQ(widget.render_model().entries.size(), 1u);
    const auto& e = widget.render_model().entries[0];
    EXPECT_TRUE(e.available);
    EXPECT_DOUBLE_EQ(e.raw_value, 42.5);
    EXPECT_DOUBLE_EQ(e.display_value, 42.5);
    EXPECT_EQ(e.unit, std::string("%"));
    EXPECT_EQ(e.label, std::string("CPU Usage"));  // 未覆寫標籤 → 自動取 Metric::name()
    EXPECT_EQ(e.display_text, std::string("42.5%"));
}

TEST(SystemStatusWidget, RefreshDegradesMissingMetricNfr03) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.r", f.backend, f.layers, f.metrics};

    Value def = Value::map(
        {{"metrics", Value::list({metric_entry({{"id", Value::string("gpu.usage")}})})}});
    ASSERT_EQ(widget.configure(def), SystemStatusStatus::Ok);

    MetricRegistry empty_registry;  // gpu.usage 未註冊 → 指標不存在
    EXPECT_EQ(widget.refresh(empty_registry), SystemStatusStatus::Ok);
    ASSERT_EQ(widget.render(), SystemStatusStatus::Ok);

    ASSERT_EQ(widget.render_model().entries.size(), 1u);
    const auto& e = widget.render_model().entries[0];
    EXPECT_FALSE(e.available);
    EXPECT_DOUBLE_EQ(e.raw_value, 0.0);
    EXPECT_EQ(e.display_text, std::string("\xE2\x80\x94"));  // '—' 佔位，不靜默消失
    EXPECT_TRUE(e.element.surface.empty());                  // 未綁定元素
    EXPECT_GT(e.text.lines.size(), 0u);                       // 佔位文字仍完成 E4-01 排版
}

TEST(SystemStatusWidget, RefreshDegradesInvalidMetricValueNfr03) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.s", f.backend, f.layers, f.metrics};

    Value def = Value::map(
        {{"metrics", Value::list({metric_entry({{"id", Value::string("battery.health")}})})}});
    ASSERT_EQ(widget.configure(def), SystemStatusStatus::Ok);

    auto battery = std::make_shared<InMemoryMetric>("battery.health", "Battery Health", "%",
                                                     MetricRange::bounded(0.0, 100.0));
    battery->add_instance("", "Battery", 1).set_value(MetricValue::unknown());  // valid=false
    MetricRegistry registry;
    ASSERT_TRUE(registry.register_metric(battery));

    EXPECT_EQ(widget.refresh(registry), SystemStatusStatus::Ok);
    ASSERT_EQ(widget.render(), SystemStatusStatus::Ok);
    EXPECT_FALSE(widget.render_model().entries[0].available);
}

TEST(SystemStatusWidget, RefreshAppliesTransformFormulaForUnitConversion) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.t", f.backend, f.layers, f.metrics};

    Value def = Value::map({
        {"vars", Value::map({{"gb", Value::number(1073741824.0)}})},
        {"metrics", Value::list({metric_entry({{"id", Value::string("mem.used")},
                                               {"transform", Value::string("= value / gb")}})})},
    });
    ASSERT_EQ(widget.configure(def), SystemStatusStatus::Ok);

    auto mem = std::make_shared<InMemoryMetric>("mem.used", "Memory Used", "GB",
                                                MetricRange::unbounded());
    mem->add_instance("", "RAM", 1).push(2147483648.0);  // 2 GiB in bytes
    MetricRegistry registry;
    ASSERT_TRUE(registry.register_metric(mem));

    ASSERT_EQ(widget.refresh(registry), SystemStatusStatus::Ok);
    ASSERT_EQ(widget.render(), SystemStatusStatus::Ok);

    const auto& e = widget.render_model().entries[0];
    EXPECT_TRUE(e.available);
    EXPECT_DOUBLE_EQ(e.raw_value, 2147483648.0);
    EXPECT_DOUBLE_EQ(e.display_value, 2.0);
    EXPECT_EQ(e.display_text, std::string("2.0 GB"));
}

// -----------------------------------------------------------------------------
// render()（E4-03 Bar/Gauge/Progress + E4-01 文字）
// -----------------------------------------------------------------------------

TEST(SystemStatusWidget, RenderInvalidBeforeRefresh) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.u", f.backend, f.layers, f.metrics};
    ASSERT_EQ(widget.configure(Value::map({{"metrics", Value::list({})}})),
              SystemStatusStatus::Ok);

    EXPECT_EQ(widget.render(), SystemStatusStatus::Invalid);
}

TEST(SystemStatusWidget, RenderBarElementFillRatioAndSlot) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.v", f.backend, f.layers, f.metrics};

    Value def = Value::map(
        {{"metrics", Value::list({metric_entry({{"id", Value::string("cpu.usage")},
                                                {"kind", Value::string("bar")},
                                                {"min", Value::number(0)},
                                                {"max", Value::number(100)}})})}});
    ASSERT_EQ(widget.configure(def), SystemStatusStatus::Ok);

    auto cpu = std::make_shared<InMemoryMetric>("cpu.usage", "CPU Usage", "%",
                                                MetricRange::bounded(0.0, 100.0));
    cpu->add_instance("", "CPU", 1).push(25.0);
    MetricRegistry registry;
    ASSERT_TRUE(registry.register_metric(cpu));

    ASSERT_EQ(widget.refresh(registry), SystemStatusStatus::Ok);
    ASSERT_EQ(widget.render(), SystemStatusStatus::Ok);

    const auto& e = widget.render_model().entries[0];
    EXPECT_EQ(e.element.kind, ElementKind::Bar);
    EXPECT_DOUBLE_EQ(e.element.fill_ratio, 0.25);
    EXPECT_EQ(e.element.surface, std::string("widget.sysstat.v.metrics"));
    EXPECT_EQ(e.element.slot, std::string("cpu.usage"));
    EXPECT_FALSE(e.element.show_label);  // 本 widget 自行以 E4-01 排版文字，不用元件自帶標籤
}

TEST(SystemStatusWidget, RenderGaugeElementComputesAngle) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.w", f.backend, f.layers, f.metrics};

    Value def = Value::map(
        {{"metrics", Value::list({metric_entry({{"id", Value::string("cpu.usage")},
                                                {"kind", Value::string("gauge")},
                                                {"min", Value::number(0)},
                                                {"max", Value::number(100)}})})}});
    ASSERT_EQ(widget.configure(def), SystemStatusStatus::Ok);

    auto cpu = std::make_shared<InMemoryMetric>("cpu.usage", "CPU Usage", "%",
                                                MetricRange::bounded(0.0, 100.0));
    cpu->add_instance("", "CPU", 1).push(50.0);
    MetricRegistry registry;
    ASSERT_TRUE(registry.register_metric(cpu));

    ASSERT_EQ(widget.refresh(registry), SystemStatusStatus::Ok);
    ASSERT_EQ(widget.render(), SystemStatusStatus::Ok);

    const auto& e = widget.render_model().entries[0];
    EXPECT_EQ(e.element.kind, ElementKind::Gauge);
    EXPECT_TRUE(e.element.has_angle);
    EXPECT_DOUBLE_EQ(e.element.fill_ratio, 0.5);
    // 預設弧：start=135, sweep=270 → angle = 135 + 0.5*270 = 270。
    EXPECT_DOUBLE_EQ(e.element.angle_degrees, 270.0);
}

TEST(SystemStatusWidget, RenderProgressElementUsesFixedPercentRange) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.x", f.backend, f.layers, f.metrics};

    Value def = Value::map(
        {{"metrics", Value::list({metric_entry({{"id", Value::string("disk.usage")},
                                                {"kind", Value::string("progress")}})})}});
    ASSERT_EQ(widget.configure(def), SystemStatusStatus::Ok);

    auto disk = std::make_shared<InMemoryMetric>("disk.usage", "Disk Usage", "%",
                                                  MetricRange::unbounded());
    disk->add_instance("", "Disk", 1).push(63.0);
    MetricRegistry registry;
    ASSERT_TRUE(registry.register_metric(disk));

    ASSERT_EQ(widget.refresh(registry), SystemStatusStatus::Ok);
    ASSERT_EQ(widget.render(), SystemStatusStatus::Ok);

    const auto& e = widget.render_model().entries[0];
    EXPECT_EQ(e.element.kind, ElementKind::Progress);
    EXPECT_DOUBLE_EQ(e.element.fill_ratio, 0.63);
}

TEST(SystemStatusWidget, RenderTextKindHasNoBoundElement) {
    Fixture f;
    SystemStatusWidget widget{"widget.sysstat.y", f.backend, f.layers, f.metrics};

    Value def = Value::map(
        {{"metrics", Value::list({metric_entry({{"id", Value::string("net.rx")},
                                                {"kind", Value::string("text")}})})}});
    ASSERT_EQ(widget.configure(def), SystemStatusStatus::Ok);

    auto net = std::make_shared<InMemoryMetric>("net.rx", "Net RX", "KB/s",
                                                 MetricRange::unbounded());
    net->add_instance("", "eth0", 1).push(512.0);
    MetricRegistry registry;
    ASSERT_TRUE(registry.register_metric(net));

    ASSERT_EQ(widget.refresh(registry), SystemStatusStatus::Ok);
    ASSERT_EQ(widget.render(), SystemStatusStatus::Ok);

    const auto& e = widget.render_model().entries[0];
    EXPECT_TRUE(e.available);
    EXPECT_TRUE(e.element.surface.empty());  // Text 種類不掛 E4-03 元素
    EXPECT_EQ(e.display_text, std::string("512.0 KB/s"));
    EXPECT_GT(e.text.lines.size(), 0u);
}
