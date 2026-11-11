// tests/c2/test_c2_11.cpp — C2-11 行程 top-N widget（gtest）
//
// 可重跑證據（單一指令，從 repo 根執行；CI 的 G3 以同一組 CTest 測試跑之）：
//   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target test_c2_11 -j 2 && ctest --test-dir build -R '^c2_11$' --output-on-failure
//
// 涵蓋：
//   - configure()：sort_by / top_n 必填與允許集合（0/3/5/10）、kind、min/max（數字 + E7-05
//     靜態公式 + FormulaError + 退化範圍）、transform 語法、未知鍵不被靜默忽略、全有或全無。
//   - configure_text()：**E7-01 文字宣告 + format_version 版本欄位**（相容 / 不相容 / 語法錯）。
//   - **切換排序維度 = 換掉註冊的指標**（proc.cpu ↔ proc.memory，單位 % ↔ MB）：維度改了就去要
//     另一個 id；舊指標還在 registry 也不讀 → ProcessMetricState::Missing，且**歷史不保留**
//     （列清空）；host 重建 provider 後才有資料（CHG-20260829-05 Q-A）。
//   - refresh()：top-N 截斷與排序（含重新取樣後名次變動）、unit 對帳（UnitMismatch）、
//     無實例（NoInstances）、單列降級（NFR-03：實例無讀值 / transform 求值失敗）。
//   - render()：Bar / Gauge / Progress / Text 的 E4-03 渲染描述、具名 surface + slot、
//     **無顯示範圍不掛元素**（K-002）、不可用列的 "—" 佔位。
//
// 本檔**不自造指標替身**：行程指標一律由**真實的 E2-19 `ProcessMonitorProvider` + 注入式
// `NullProcessSource`** 註冊進 E2-01 `MetricRegistry`（知識庫 K-003 / K-007：只測自帶 stub 的
// 契約測試會空轉，過度寬容的替身會把 bug 養大）。唯一手搓的指標只用於**負向**案例
// （id 對、單位不對）——那是真實提供者按定義產不出來的情形。
#include "process_top_widget.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using ds::elements::ElementKind;
using ds::format::Value;
using ds::metrics::InMemoryMetric;
using ds::metrics::MetricRange;
using ds::metrics::MetricRegistry;
using ds::sysinfo::NullProcessSource;
using ds::sysinfo::ProcessInfo;
using ds::sysinfo::ProcessMonitorProvider;
using ds::sysinfo::ProcessSortKey;
using ds::sysinfo::ProcessStatus;
using ds::widgets::ProcessMetricState;
using ds::widgets::ProcessRowElementKind;
using ds::widgets::ProcessTopStatus;
using ds::widgets::ProcessTopWidget;

namespace {

constexpr double kMiB = 1024.0 * 1024.0;

ProcessInfo proc(std::uint64_t pid, const std::string& name, double cpu,
                 double memory_mib = 1.0, ProcessStatus st = ProcessStatus::Running) {
    ProcessInfo p;
    p.pid = pid;
    p.name = name;
    p.cpu_percent = cpu;
    p.memory_bytes = static_cast<std::uint64_t>(memory_mib * kMiB);
    p.status = st;
    return p;
}

// 真實 E2-19 提供者 + 注入式 null 來源 → 真實 E2-01 註冊表。
// provider 的 top_n 固定為「全部」：**要測的 N 是 widget 的顯示 N**（provider 自己的 top_n 是
// 另一回事，由 host 建構時決定）。
struct Upstream {
    std::shared_ptr<NullProcessSource> source = std::make_shared<NullProcessSource>();
    std::shared_ptr<ProcessMonitorProvider> provider;
    MetricRegistry registry;

    void install(ProcessSortKey key, std::vector<ProcessInfo> procs) {
        source->set_processes(std::move(procs));
        provider = std::make_shared<ProcessMonitorProvider>(
            source, key, ProcessMonitorProvider::kAllProcesses);
        provider->register_metrics(registry);
    }

    void install_sequence(ProcessSortKey key, std::vector<std::vector<ProcessInfo>> seq) {
        source->set_sequence(std::move(seq));
        provider = std::make_shared<ProcessMonitorProvider>(
            source, key, ProcessMonitorProvider::kAllProcesses);
        provider->register_metrics(registry);
    }
};

Value cfg(std::vector<Value::Member> fields) { return Value::map(std::move(fields)); }

// 最小合法設定：維度 + N。
Value cfg_min(const char* sort_by, int top_n) {
    return cfg({{"sort_by", Value::string(sort_by)}, {"top_n", Value::integer(top_n)}});
}

}  // namespace

// -----------------------------------------------------------------------------
// 建構 / 靜態對照
// -----------------------------------------------------------------------------

TEST(ProcessTopWidget, ConstructedHasDefaults) {
    ProcessTopWidget w{"widget.proctop.main"};

    EXPECT_EQ(w.id(), std::string("widget.proctop.main"));
    EXPECT_FALSE(w.is_configured());
    EXPECT_FALSE(w.has_refreshed());
    EXPECT_EQ(w.metric_state(), ProcessMetricState::NotConfigured);
    EXPECT_TRUE(w.render_model().rows.empty());
    EXPECT_EQ(w.row_count(), 0u);
    EXPECT_TRUE(w.last_error().empty());
}

TEST(ProcessTopWidget, AllowedTopNIsTheAcceptanceSet) {
    const std::vector<std::size_t>& allowed = ProcessTopWidget::allowed_top_n();
    EXPECT_EQ(allowed.size(), 4u);
    EXPECT_TRUE(ProcessTopWidget::is_allowed_top_n(0));  // 0 = 全部
    EXPECT_TRUE(ProcessTopWidget::is_allowed_top_n(3));
    EXPECT_TRUE(ProcessTopWidget::is_allowed_top_n(5));
    EXPECT_TRUE(ProcessTopWidget::is_allowed_top_n(10));
    EXPECT_FALSE(ProcessTopWidget::is_allowed_top_n(1));
    EXPECT_FALSE(ProcessTopWidget::is_allowed_top_n(7));
}

// 維度 ↔ 指標身分的對照必須與 E2-19 提供者自己回報的一致（否則 widget 會去要一個
// 不存在的 id，而且是安靜地要）。
TEST(ProcessTopWidget, MetricIdAndUnitMatchE2_19Provider) {
    auto src = std::make_shared<NullProcessSource>();

    ProcessMonitorProvider cpu_provider{src, ProcessSortKey::Cpu};
    EXPECT_EQ(ProcessTopWidget::metric_id_for(ProcessSortKey::Cpu), cpu_provider.metric_id());
    EXPECT_EQ(std::string(ProcessTopWidget::unit_for(ProcessSortKey::Cpu)), std::string("%"));

    ProcessMonitorProvider mem_provider{src, ProcessSortKey::Memory};
    EXPECT_EQ(ProcessTopWidget::metric_id_for(ProcessSortKey::Memory), mem_provider.metric_id());
    EXPECT_EQ(std::string(ProcessTopWidget::unit_for(ProcessSortKey::Memory)), std::string("MB"));

    EXPECT_NE(cpu_provider.metric_id(), mem_provider.metric_id());  // 維度不同 = 指標不同
}

// -----------------------------------------------------------------------------
// configure()
// -----------------------------------------------------------------------------

TEST(ProcessTopWidget, ConfigureMinimalCpu) {
    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 5)), ProcessTopStatus::Ok);

    EXPECT_TRUE(w.is_configured());
    EXPECT_EQ(w.spec().sort_key, ProcessSortKey::Cpu);
    EXPECT_EQ(w.spec().top_n, 5u);
    EXPECT_EQ(w.spec().kind, ProcessRowElementKind::Bar);  // 預設
    EXPECT_FALSE(w.spec().has_range_override);
    EXPECT_TRUE(w.spec().transform.empty());
    EXPECT_EQ(w.metric_id(), std::string("proc.cpu"));
    EXPECT_EQ(std::string(w.expected_unit()), std::string("%"));
    EXPECT_TRUE(w.last_error().empty());
}

// 核心：**切換排序維度 = 換掉要讀的指標**（連單位一起換）。
TEST(ProcessTopWidget, ConfigureMemorySwapsMetricIdentityAndUnit) {
    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 3)), ProcessTopStatus::Ok);
    ASSERT_EQ(w.metric_id(), std::string("proc.cpu"));
    ASSERT_EQ(std::string(w.expected_unit()), std::string("%"));

    ASSERT_EQ(w.configure(cfg_min("memory", 3)), ProcessTopStatus::Ok);
    EXPECT_EQ(w.spec().sort_key, ProcessSortKey::Memory);
    EXPECT_EQ(w.metric_id(), std::string("proc.memory"));
    EXPECT_EQ(std::string(w.expected_unit()), std::string("MB"));
}

TEST(ProcessTopWidget, ConfigureAllFields) {
    ProcessTopWidget w{"w"};
    Value def = cfg({
        {"sort_by", Value::string("memory")},
        {"top_n", Value::integer(10)},
        {"kind", Value::string("gauge")},
        {"min", Value::number(0)},
        {"max", Value::string("= 1024 * 8")},  // E7-05 靜態公式：8 GB（單位 MB）
        {"transform", Value::string("= value / 1024")},
    });
    ASSERT_EQ(w.configure(def), ProcessTopStatus::Ok);

    EXPECT_EQ(w.spec().top_n, 10u);
    EXPECT_EQ(w.spec().kind, ProcessRowElementKind::Gauge);
    EXPECT_TRUE(w.spec().has_range_override);
    EXPECT_DOUBLE_EQ(w.spec().range_min, 0.0);
    EXPECT_DOUBLE_EQ(w.spec().range_max, 8192.0);
    EXPECT_EQ(w.spec().transform, std::string("= value / 1024"));
}

TEST(ProcessTopWidget, ConfigureTopNZeroMeansAllProcesses) {
    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 0)), ProcessTopStatus::Ok);
    EXPECT_EQ(w.spec().top_n, ProcessTopWidget::kAllProcesses);
    EXPECT_EQ(w.spec().top_n, 0u);
}

TEST(ProcessTopWidget, ConfigureRejectsNonMapRoot) {
    ProcessTopWidget w{"w"};
    EXPECT_EQ(w.configure(Value::string("nope")), ProcessTopStatus::Invalid);
    EXPECT_FALSE(w.is_configured());
    EXPECT_FALSE(w.last_error().empty());
}

TEST(ProcessTopWidget, ConfigureRequiresSortBy) {
    ProcessTopWidget w{"w"};
    EXPECT_EQ(w.configure(cfg({{"top_n", Value::integer(3)}})), ProcessTopStatus::Invalid);
    EXPECT_FALSE(w.is_configured());
}

TEST(ProcessTopWidget, ConfigureRejectsUnknownSortDimension) {
    ProcessTopWidget w{"w"};
    EXPECT_EQ(w.configure(cfg_min("disk", 3)), ProcessTopStatus::Invalid);
    EXPECT_EQ(w.configure(cfg({{"sort_by", Value::integer(1)}, {"top_n", Value::integer(3)}})),
              ProcessTopStatus::Invalid);
}

TEST(ProcessTopWidget, ConfigureRequiresTopN) {
    ProcessTopWidget w{"w"};
    EXPECT_EQ(w.configure(cfg({{"sort_by", Value::string("cpu")}})), ProcessTopStatus::Invalid);
}

TEST(ProcessTopWidget, ConfigureRejectsTopNOutsideAllowedSet) {
    ProcessTopWidget w{"w"};
    EXPECT_EQ(w.configure(cfg_min("cpu", 7)), ProcessTopStatus::Invalid);
    EXPECT_EQ(w.configure(cfg_min("cpu", -1)), ProcessTopStatus::Invalid);
    EXPECT_EQ(w.configure(cfg({{"sort_by", Value::string("cpu")},
                                {"top_n", Value::number(3.5)}})),
              ProcessTopStatus::Invalid);
    EXPECT_EQ(w.configure(cfg({{"sort_by", Value::string("cpu")},
                                {"top_n", Value::string("3")}})),
              ProcessTopStatus::Invalid);
    EXPECT_FALSE(w.is_configured());
}

// 非整數旗標但值為整數的 Number 仍須接受（判語意，不判來源）。
TEST(ProcessTopWidget, ConfigureAcceptsIntegralNumberForTopN) {
    ProcessTopWidget w{"w"};
    EXPECT_EQ(w.configure(cfg({{"sort_by", Value::string("cpu")}, {"top_n", Value::number(5)}})),
              ProcessTopStatus::Ok);
    EXPECT_EQ(w.spec().top_n, 5u);
}

TEST(ProcessTopWidget, ConfigureRejectsUnknownFieldInsteadOfIgnoringIt) {
    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("cpu")},
                      {"top_n", Value::integer(3)},
                      {"sortby", Value::string("memory")}});  // 拼錯的鍵
    EXPECT_EQ(w.configure(def), ProcessTopStatus::Invalid);
    EXPECT_NE(w.last_error().find("sortby"), std::string::npos);
    EXPECT_FALSE(w.is_configured());
}

TEST(ProcessTopWidget, ConfigureRejectsUnknownKind) {
    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("cpu")},
                      {"top_n", Value::integer(3)},
                      {"kind", Value::string("sparkline")}});
    EXPECT_EQ(w.configure(def), ProcessTopStatus::Invalid);
}

TEST(ProcessTopWidget, ConfigureRequiresMinMaxTogether) {
    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("memory")},
                      {"top_n", Value::integer(3)},
                      {"min", Value::number(0)}});
    EXPECT_EQ(w.configure(def), ProcessTopStatus::Invalid);
}

TEST(ProcessTopWidget, ConfigureRejectsDegenerateRange) {
    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("memory")},
                      {"top_n", Value::integer(3)},
                      {"min", Value::number(100)},
                      {"max", Value::number(100)}});
    EXPECT_EQ(w.configure(def), ProcessTopStatus::Invalid);
}

// min/max 公式是**靜態**的：求值作用域不含 `value`，引用它即為未定義變數 → FormulaError。
TEST(ProcessTopWidget, ConfigureMinMaxFormulaIsStaticAndErrorsAreNamed) {
    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("memory")},
                      {"top_n", Value::integer(3)},
                      {"min", Value::number(0)},
                      {"max", Value::string("= value * 2")}});
    EXPECT_EQ(w.configure(def), ProcessTopStatus::FormulaError);
    EXPECT_FALSE(w.last_error().empty());
    EXPECT_FALSE(w.is_configured());

    Value div0 = cfg({{"sort_by", Value::string("memory")},
                       {"top_n", Value::integer(3)},
                       {"min", Value::number(0)},
                       {"max", Value::string("= 1024 / 0")}});
    EXPECT_EQ(w.configure(div0), ProcessTopStatus::FormulaError);
}

TEST(ProcessTopWidget, ConfigureRejectsNonFormulaTransform) {
    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("cpu")},
                      {"top_n", Value::integer(3)},
                      {"transform", Value::string("value / 2")}});  // 缺公式標記
    EXPECT_EQ(w.configure(def), ProcessTopStatus::Invalid);
}

TEST(ProcessTopWidget, FailedConfigureLeavesPreviousConfigIntact) {
    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 5)), ProcessTopStatus::Ok);

    EXPECT_EQ(w.configure(cfg_min("memory", 7)), ProcessTopStatus::Invalid);  // N 不合法
    EXPECT_TRUE(w.is_configured());
    EXPECT_EQ(w.spec().sort_key, ProcessSortKey::Cpu);  // 全有或全無：維度沒被改掉
    EXPECT_EQ(w.spec().top_n, 5u);
    EXPECT_EQ(w.metric_id(), std::string("proc.cpu"));
}

// -----------------------------------------------------------------------------
// configure_text()：E7-01 宣告式格式核心 + 版本欄位
// -----------------------------------------------------------------------------

TEST(ProcessTopWidget, ConfigureFromE7_01TextDocument) {
    ProcessTopWidget w{"w"};
    const std::string text =
        "format_version: 1.0\n"
        "sort_by: memory\n"
        "top_n: 3\n"
        "kind: progress\n"
        "min: 0\n"
        "max: \"= 1024 * 4\"\n";

    ASSERT_EQ(w.configure_text(text), ProcessTopStatus::Ok) << w.last_error();
    EXPECT_EQ(w.spec().sort_key, ProcessSortKey::Memory);
    EXPECT_EQ(w.spec().top_n, 3u);
    EXPECT_EQ(w.spec().kind, ProcessRowElementKind::Progress);
    EXPECT_TRUE(w.spec().has_range_override);
    EXPECT_DOUBLE_EQ(w.spec().range_max, 4096.0);
    EXPECT_EQ(w.metric_id(), std::string("proc.memory"));
}

TEST(ProcessTopWidget, ConfigureTextRejectsMissingOrIncompatibleFormatVersion) {
    ProcessTopWidget w{"w"};

    // 缺版本欄位（E7-01 根層唯一必填欄位）。
    EXPECT_EQ(w.configure_text("sort_by: cpu\ntop_n: 3\n"), ProcessTopStatus::FormatError);
    EXPECT_FALSE(w.last_error().empty());

    // 主版號不同 = 不相容。
    EXPECT_EQ(w.configure_text("format_version: 2.0\nsort_by: cpu\ntop_n: 3\n"),
              ProcessTopStatus::FormatError);

    // 語法錯（tab 縮排）→ 帶行號，不靜默。
    EXPECT_EQ(w.configure_text("format_version: 1.0\nwindow:\n\tsort_by: cpu\n"),
              ProcessTopStatus::FormatError);
    EXPECT_NE(w.last_error().find("line"), std::string::npos);

    EXPECT_FALSE(w.is_configured());
}

TEST(ProcessTopWidget, ConfigureTextStillValidatesFields) {
    ProcessTopWidget w{"w"};
    // 版本沒問題，但 N 不在允許集合內 → 不是 FormatError，是 Invalid。
    EXPECT_EQ(w.configure_text("format_version: 1.0\nsort_by: cpu\ntop_n: 4\n"),
              ProcessTopStatus::Invalid);
}

// -----------------------------------------------------------------------------
// refresh()：讀真實 E2-19 提供者註冊的指標
// -----------------------------------------------------------------------------

TEST(ProcessTopWidget, RefreshBeforeConfigureIsInvalid) {
    Upstream up;
    up.install(ProcessSortKey::Cpu, {proc(1, "a", 10.0)});

    ProcessTopWidget w{"w"};
    EXPECT_EQ(w.refresh(up.registry), ProcessTopStatus::Invalid);
    EXPECT_EQ(w.metric_state(), ProcessMetricState::NotConfigured);
    EXPECT_FALSE(w.has_refreshed());
}

TEST(ProcessTopWidget, RefreshOnEmptyRegistryReportsMissingNotZero) {
    MetricRegistry empty;
    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 5)), ProcessTopStatus::Ok);

    EXPECT_EQ(w.refresh(empty), ProcessTopStatus::Ok);  // 流程跑完
    EXPECT_EQ(w.metric_state(), ProcessMetricState::Missing);  // 但找到什麼有名字
    EXPECT_EQ(w.row_count(), 0u);
    EXPECT_FALSE(w.last_error().empty());
    EXPECT_EQ(w.render_model().metric_id, std::string("proc.cpu"));
}

TEST(ProcessTopWidget, RefreshTakesTopNSortedByDimension) {
    Upstream up;
    up.install(ProcessSortKey::Cpu, {
                                        proc(11, "idle", 1.0),
                                        proc(12, "busy", 71.5),
                                        proc(13, "mid", 30.0),
                                        proc(14, "hot", 88.5),
                                        proc(15, "low", 5.0),
                                    });

    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 3)), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.metric_state(), ProcessMetricState::Ok);

    ASSERT_EQ(w.row_count(), 3u);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);
    const auto& rows = w.render_model().rows;
    ASSERT_EQ(rows.size(), 3u);

    EXPECT_EQ(rows[0].instance_id, std::string("14"));
    EXPECT_EQ(rows[0].label, std::string("hot"));
    EXPECT_DOUBLE_EQ(rows[0].raw_value, 88.5);
    EXPECT_EQ(rows[1].instance_id, std::string("12"));
    EXPECT_EQ(rows[2].instance_id, std::string("13"));

    EXPECT_EQ(w.render_model().unit, std::string("%"));
    EXPECT_EQ(rows[0].unit, std::string("%"));
    EXPECT_EQ(rows[0].display_text, std::string("88.5%"));
}

TEST(ProcessTopWidget, RefreshTopNZeroKeepsEveryProcess) {
    Upstream up;
    up.install(ProcessSortKey::Cpu,
               {proc(1, "a", 1.0), proc(2, "b", 2.0), proc(3, "c", 3.0), proc(4, "d", 4.0)});

    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 0)), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    EXPECT_EQ(w.row_count(), 4u);
}

// 同值以 pid 遞增為次序（決定性，與 E2-19 top_processes() 的規則一致）。
TEST(ProcessTopWidget, RefreshBreaksTiesByPidAscending) {
    Upstream up;
    up.install(ProcessSortKey::Cpu, {proc(42, "later", 50.0), proc(7, "earlier", 50.0)});

    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 3)), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);

    ASSERT_EQ(w.render_model().rows.size(), 2u);
    EXPECT_EQ(w.render_model().rows[0].instance_id, std::string("7"));
    EXPECT_EQ(w.render_model().rows[1].instance_id, std::string("42"));
}

// 名次會變：provider 的實例建立順序是第一份快照的順序，widget 必須以**當次值**重排。
TEST(ProcessTopWidget, RefreshReordersAfterProviderResample) {
    Upstream up;
    up.install_sequence(ProcessSortKey::Cpu,
                        {
                            {proc(1, "first", 90.0), proc(2, "second", 10.0)},
                            {proc(1, "first", 5.0), proc(2, "second", 80.0)},
                        });

    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 3)), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render_model().rows.size(), 2u);
    EXPECT_EQ(w.render_model().rows[0].instance_id, std::string("1"));

    up.provider->sample();  // host 依 E2-02 排程重新取樣（第二份快照）
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render_model().rows.size(), 2u);
    EXPECT_EQ(w.render_model().rows[0].instance_id, std::string("2"));  // 名次換了
    EXPECT_DOUBLE_EQ(w.render_model().rows[0].raw_value, 80.0);
}

// NFR-03：實例「本次無讀值」（E2-19 對缺席的 pid 設 unknown）→ 該列降級、置後，其餘列不受影響。
TEST(ProcessTopWidget, RowWithoutReadingDegradesButOthersSurvive) {
    Upstream up;
    up.install_sequence(ProcessSortKey::Cpu,
                        {
                            {proc(1, "stays", 40.0), proc(2, "exits", 60.0)},
                            {proc(1, "stays", 40.0)},  // pid 2 不見了 → E2-19 設為 unknown
                        });
    up.provider->sample();

    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 5)), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    EXPECT_EQ(w.metric_state(), ProcessMetricState::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);

    const auto& rows = w.render_model().rows;
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].instance_id, std::string("1"));
    EXPECT_TRUE(rows[0].available);
    EXPECT_EQ(rows[1].instance_id, std::string("2"));
    EXPECT_FALSE(rows[1].available);                              // 無讀值 ≠ 0%
    EXPECT_EQ(rows[1].display_text, std::string("\xE2\x80\x94"));  // "—" 佔位
    EXPECT_TRUE(rows[1].element.surface.empty());                 // 未綁定任何 surface
    EXPECT_DOUBLE_EQ(rows[1].display_value, 0.0);
}

TEST(ProcessTopWidget, RefreshReportsNoInstancesWhenSourceHasNoReading) {
    Upstream up;
    up.install(ProcessSortKey::Cpu, {});  // 空快照 → 無實例
    // 註冊表裡確實有指標，但一個行程實例都沒有。
    ASSERT_TRUE(up.registry.contains("proc.cpu"));

    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 3)), ProcessTopStatus::Ok);
    EXPECT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    EXPECT_EQ(w.metric_state(), ProcessMetricState::NoInstances);
    EXPECT_EQ(w.row_count(), 0u);
}

// id 對、單位不對 → 那不是 E2-19 為本維度掛的指標。不讀、不猜、不畫（K-002）。
TEST(ProcessTopWidget, RefreshRejectsMetricWithWrongUnit) {
    MetricRegistry registry;
    auto impostor = std::make_shared<InMemoryMetric>("proc.cpu", "Process CPU", "MB",
                                                      MetricRange::at_least(0.0));
    impostor->add_instance("1", "a", 8);
    registry.register_metric(impostor);

    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 3)), ProcessTopStatus::Ok);
    EXPECT_EQ(w.refresh(registry), ProcessTopStatus::Ok);
    EXPECT_EQ(w.metric_state(), ProcessMetricState::UnitMismatch);
    EXPECT_EQ(w.row_count(), 0u);
    EXPECT_FALSE(w.last_error().empty());
}

// -----------------------------------------------------------------------------
// 切換排序維度 = 換掉註冊的指標；歷史不保留；provider 由 host 重建（Q-A）
// -----------------------------------------------------------------------------

TEST(ProcessTopWidget, SwitchingDimensionDropsRowsUntilHostRebuildsProvider) {
    Upstream up;
    up.install(ProcessSortKey::Cpu, {proc(1, "a", 70.0, 512.0), proc(2, "b", 20.0, 2048.0)});

    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 3)), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);
    ASSERT_EQ(w.row_count(), 2u);
    ASSERT_EQ(w.render_model().unit, std::string("%"));

    // 設定改為記憶體維度：舊的 proc.cpu 指標還在 registry 裡——但**不該**被讀
    //（讀了單位會從 % 偷偷變成 MB）。
    ASSERT_EQ(w.configure(cfg_min("memory", 3)), ProcessTopStatus::Ok);
    EXPECT_FALSE(w.has_refreshed());        // 歷史不保留：設定一改就得重新 refresh()
    EXPECT_EQ(w.row_count(), 0u);
    EXPECT_TRUE(w.render_model().rows.empty());

    ASSERT_TRUE(up.registry.contains("proc.cpu"));
    EXPECT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    EXPECT_EQ(w.metric_state(), ProcessMetricState::Missing);  // host 還沒重建 provider
    EXPECT_EQ(w.row_count(), 0u);
    EXPECT_EQ(w.render_model().metric_id, std::string("proc.memory"));

    // host（H1-11）重建 provider 並重新註冊 → 這時才有資料，而且單位是 MB。
    auto mem_source = std::make_shared<NullProcessSource>(
        std::vector<ProcessInfo>{proc(1, "a", 70.0, 512.0), proc(2, "b", 20.0, 2048.0)});
    ProcessMonitorProvider rebuilt{mem_source, ProcessSortKey::Memory,
                                    ProcessMonitorProvider::kAllProcesses};
    rebuilt.register_metrics(up.registry);

    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    EXPECT_EQ(w.metric_state(), ProcessMetricState::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);
    const auto& rows = w.render_model().rows;
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(w.render_model().unit, std::string("MB"));
    EXPECT_EQ(rows[0].instance_id, std::string("2"));  // 記憶體最大的在前（換維度 = 換名次）
    EXPECT_DOUBLE_EQ(rows[0].raw_value, 2048.0);
    EXPECT_EQ(rows[0].display_text, std::string("2048.0 MB"));
}

// -----------------------------------------------------------------------------
// transform（E7-05 動態公式）
// -----------------------------------------------------------------------------

TEST(ProcessTopWidget, TransformConvertsEachRowValue) {
    Upstream up;
    up.install(ProcessSortKey::Memory, {proc(1, "big", 1.0, 2048.0), proc(2, "small", 1.0, 512.0)});

    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("memory")},
                      {"top_n", Value::integer(3)},
                      {"kind", Value::string("text")},
                      {"transform", Value::string("= value / 1024")}});  // MB → GB
    ASSERT_EQ(w.configure(def), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);

    const auto& rows = w.render_model().rows;
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_DOUBLE_EQ(rows[0].raw_value, 2048.0);      // 原始值（排序仍用它）
    EXPECT_DOUBLE_EQ(rows[0].display_value, 2.0);     // 轉換後
    EXPECT_DOUBLE_EQ(rows[1].display_value, 0.5);
}

TEST(ProcessTopWidget, TransformFailureDegradesOnlyThatRow) {
    Upstream up;
    up.install(ProcessSortKey::Cpu, {proc(1, "a", 50.0), proc(2, "b", 10.0)});

    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("cpu")},
                      {"top_n", Value::integer(3)},
                      {"transform", Value::string("= value / 0")}});  // 除零：每列都失敗
    ASSERT_EQ(w.configure(def), ProcessTopStatus::Ok);  // 語法合法，求值延後
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);

    const auto& rows = w.render_model().rows;
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_FALSE(rows[0].available);
    EXPECT_FALSE(rows[1].available);
    EXPECT_EQ(rows[0].display_text, std::string("\xE2\x80\x94"));
    EXPECT_FALSE(w.last_error().empty());  // 原因有被記下來，不是靜默吞掉
}

// -----------------------------------------------------------------------------
// render()：E4-03 組裝
// -----------------------------------------------------------------------------

TEST(ProcessTopWidget, RenderBeforeRefreshIsInvalid) {
    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 3)), ProcessTopStatus::Ok);
    EXPECT_EQ(w.render(), ProcessTopStatus::Invalid);
    EXPECT_TRUE(w.render_model().rows.empty());
}

TEST(ProcessTopWidget, RenderBarUsesMetricBoundedRangeAndNamedTargets) {
    Upstream up;
    up.install(ProcessSortKey::Cpu, {proc(9, "hot", 25.0)});

    ProcessTopWidget w{"widget.proctop.main"};
    ASSERT_EQ(w.configure(cfg_min("cpu", 3)), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);

    ASSERT_EQ(w.render_model().rows.size(), 1u);
    const auto& row = w.render_model().rows[0];
    EXPECT_TRUE(row.has_range);                     // proc.cpu 的 range 是 bounded[0,100]
    EXPECT_DOUBLE_EQ(row.range_min, 0.0);
    EXPECT_DOUBLE_EQ(row.range_max, 100.0);
    EXPECT_EQ(row.element.kind, ElementKind::Bar);
    EXPECT_DOUBLE_EQ(row.element.fill_ratio, 0.25);
    EXPECT_EQ(row.element.surface, std::string("widget.proctop.main.rows"));
    EXPECT_EQ(row.element.slot, std::string("9"));  // 具名 slot = pid（NFR-02：無絕對座標）
    EXPECT_EQ(row.element.label, std::string("25.0%"));
}

// K-002：記憶體維度無上界（at_least(0)）。沒宣告 min/max 就**不畫長條**——否則 8 GB 與
// 100 MB 會畫成一樣滿，值域檢查全過、畫面看起來正常，而那是假資料。
TEST(ProcessTopWidget, MemoryWithoutDeclaredRangeRendersTextOnly) {
    Upstream up;
    up.install(ProcessSortKey::Memory, {proc(1, "big", 1.0, 8192.0)});

    ProcessTopWidget w{"w"};
    ASSERT_EQ(w.configure(cfg_min("memory", 3)), ProcessTopStatus::Ok);  // kind 預設 bar
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);

    ASSERT_EQ(w.render_model().rows.size(), 1u);
    const auto& row = w.render_model().rows[0];
    EXPECT_TRUE(row.available);
    EXPECT_FALSE(row.has_range);
    EXPECT_TRUE(row.element.surface.empty());  // 未綁定 = 不畫
    EXPECT_EQ(row.display_text, std::string("8192.0 MB"));  // 文字仍誠實呈現
}

TEST(ProcessTopWidget, MemoryWithDeclaredRangeRendersBar) {
    Upstream up;
    up.install(ProcessSortKey::Memory, {proc(1, "big", 1.0, 2048.0)});

    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("memory")},
                      {"top_n", Value::integer(3)},
                      {"min", Value::number(0)},
                      {"max", Value::string("= 1024 * 8")}});  // 8 GB 上界，明寫出來
    ASSERT_EQ(w.configure(def), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);

    const auto& row = w.render_model().rows[0];
    EXPECT_TRUE(row.has_range);
    EXPECT_DOUBLE_EQ(row.range_max, 8192.0);
    EXPECT_EQ(row.element.kind, ElementKind::Bar);
    EXPECT_DOUBLE_EQ(row.element.fill_ratio, 0.25);  // 2048 / 8192
}

TEST(ProcessTopWidget, RenderGaugeProducesAngle) {
    Upstream up;
    up.install(ProcessSortKey::Cpu, {proc(1, "a", 50.0)});

    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("cpu")},
                      {"top_n", Value::integer(3)},
                      {"kind", Value::string("gauge")}});
    ASSERT_EQ(w.configure(def), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);

    const auto& row = w.render_model().rows[0];
    EXPECT_EQ(row.element.kind, ElementKind::Gauge);
    EXPECT_TRUE(row.element.has_angle);
    EXPECT_DOUBLE_EQ(row.element.fill_ratio, 0.5);
    EXPECT_DOUBLE_EQ(row.element.angle_degrees,
                     row.element.start_angle_degrees + 0.5 * row.element.sweep_degrees);
}

// Progress 的範圍固定 [0,100]，故以「值於顯示範圍內的比例」換算百分比——這樣它對
// % 與 MB 兩個維度都表達同一件事：佔宣告範圍的幾成。
TEST(ProcessTopWidget, RenderProgressUsesProportionOfDeclaredRange) {
    Upstream up;
    up.install(ProcessSortKey::Memory, {proc(1, "big", 1.0, 1024.0)});

    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("memory")},
                      {"top_n", Value::integer(3)},
                      {"kind", Value::string("progress")},
                      {"min", Value::number(0)},
                      {"max", Value::number(4096)}});
    ASSERT_EQ(w.configure(def), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);

    const auto& row = w.render_model().rows[0];
    EXPECT_EQ(row.element.kind, ElementKind::Progress);
    EXPECT_DOUBLE_EQ(row.element.fill_ratio, 0.25);  // 1024 / 4096
    EXPECT_EQ(row.display_text, std::string("1024.0 MB"));
}

TEST(ProcessTopWidget, RenderTextKindLeavesElementUnbound) {
    Upstream up;
    up.install(ProcessSortKey::Cpu, {proc(1, "a", 33.0)});

    ProcessTopWidget w{"w"};
    Value def = cfg({{"sort_by", Value::string("cpu")},
                      {"top_n", Value::integer(3)},
                      {"kind", Value::string("text")}});
    ASSERT_EQ(w.configure(def), ProcessTopStatus::Ok);
    ASSERT_EQ(w.refresh(up.registry), ProcessTopStatus::Ok);
    ASSERT_EQ(w.render(), ProcessTopStatus::Ok);

    const auto& row = w.render_model().rows[0];
    EXPECT_TRUE(row.element.surface.empty());
    EXPECT_EQ(row.display_text, std::string("33.0%"));
    EXPECT_EQ(row.label, std::string("a"));
}

// 具名列舉的 to_string（NFR-02：診斷一律具名，不靠數字）。
TEST(ProcessTopWidget, NamedEnumsHaveStableStrings) {
    EXPECT_EQ(std::string(ds::widgets::to_string(ProcessRowElementKind::Progress)),
              std::string("progress"));
    EXPECT_EQ(std::string(ds::widgets::to_string(ProcessTopStatus::FormulaError)),
              std::string("FormulaError"));
    EXPECT_EQ(std::string(ds::widgets::to_string(ProcessMetricState::UnitMismatch)),
              std::string("UnitMismatch"));
    EXPECT_EQ(std::string(ds::widgets::to_string(ProcessMetricState::Missing)),
              std::string("Missing"));
}
