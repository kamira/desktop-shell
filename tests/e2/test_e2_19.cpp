// E2-19 行程監看 — 測試（gtest）
//
// 覆蓋：提供者身分 / 指標身分依排序維度、註冊到 E2-01 registry、行程列舉各欄位
// （pid / 名稱 / CPU% / 記憶體 / 狀態）、top-N 依 CPU 排序、top-N 依記憶體排序、
// 排序平手以 pid 決定性、行程數變動（增 / 減）、無讀值誠實 invalid、經 E2-02 採樣
// （除頻排程）、null 來源行為、記憶體 bytes→MB 換算、範圍、消費者只走 E2-01 抽象、
// 重複註冊保守拒絕、狀態字串、來源序列語意。相位 1：只驗介面 + 注入式來源行為，不含
// 任何平台分支 / 真實行程 API。
#include "process_monitor.hpp"

#include <gtest/gtest.h>

#include <cstdint>
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
using ds::sysinfo::NullProcessSource;
using ds::sysinfo::ProcessInfo;
using ds::sysinfo::ProcessMonitorProvider;
using ds::sysinfo::ProcessSample;
using ds::sysinfo::ProcessSortKey;
using ds::sysinfo::ProcessSource;
using ds::sysinfo::ProcessStatus;
using ds::sysinfo::process_sort_value;
using ds::sysinfo::to_string;
using ds::sysinfo::top_processes;

namespace {

constexpr double kEps = 1e-9;

// 便利建構一個行程。
ProcessInfo proc(std::uint64_t pid, std::string name, double cpu,
                 std::uint64_t mem_bytes, ProcessStatus st = ProcessStatus::Running) {
    return ProcessInfo{pid, std::move(name), cpu, mem_bytes, st};
}

// 一個 null 來源（單一固定快照）。
std::shared_ptr<NullProcessSource> makeSource(std::vector<ProcessInfo> procs) {
    return std::make_shared<NullProcessSource>(std::move(procs));
}

}  // namespace

// ===========================================================================
// 提供者 / 指標身分
// ===========================================================================
TEST(ProcessMonitorProvider, ProviderIdStableAndIsMetricProvider) {
    ProcessMonitorProvider p{std::make_shared<NullProcessSource>()};
    EXPECT_EQ(p.provider_id(), "sysinfo.proc");
    MetricProvider& mp = p;  // 可上轉為 E2-01 契約
    EXPECT_EQ(mp.provider_id(), "sysinfo.proc");
}

TEST(ProcessMonitorProvider, MetricIdDependsOnSortKey) {
    ProcessMonitorProvider cpu{std::make_shared<NullProcessSource>(), ProcessSortKey::Cpu};
    ProcessMonitorProvider mem{std::make_shared<NullProcessSource>(), ProcessSortKey::Memory};
    EXPECT_EQ(cpu.metric_id(), "proc.cpu");
    EXPECT_EQ(mem.metric_id(), "proc.memory");
    EXPECT_EQ(cpu.sort_key(), ProcessSortKey::Cpu);
    EXPECT_EQ(mem.sort_key(), ProcessSortKey::Memory);
}

TEST(ProcessMonitorProvider, SamplingTierDefaultsNormalOverridable) {
    ProcessMonitorProvider def{std::make_shared<NullProcessSource>()};
    EXPECT_EQ(def.sampling_tier(), SamplingTier::Normal);
    ProcessMonitorProvider hi{std::make_shared<NullProcessSource>(), ProcessSortKey::Cpu,
                              ProcessMonitorProvider::kAllProcesses,
                              ProcessMonitorProvider::kDefaultHistory, SamplingTier::High};
    EXPECT_EQ(hi.sampling_tier(), SamplingTier::High);
}

// ===========================================================================
// 註冊 / 行程列舉各欄位
// ===========================================================================
TEST(ProcessMonitorProvider, RegistersMetricAndEnumeratesFields) {
    auto src = makeSource({
        proc(101, "launchd", 1.5, 20u * 1024 * 1024, ProcessStatus::Running),
        proc(202, "WindowServer", 12.0, 300u * 1024 * 1024, ProcessStatus::Sleeping),
    });
    ProcessMonitorProvider p{src, ProcessSortKey::Cpu};  // top_n=0 → 全部
    MetricRegistry reg;
    const std::size_t added = reg.add_provider(p);

    EXPECT_EQ(added, 1u);
    EXPECT_TRUE(reg.contains("proc.cpu"));
    auto m = reg.get("proc.cpu");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->instance_count(), 2u);
    EXPECT_EQ(p.process_count(), 2u);

    // 依 CPU 排序：WindowServer(12%) 在前、launchd(1.5%) 在後。
    // pid 作 instance_id、名稱作 label、CPU% 作 value.number、狀態字串作 value.text。
    const auto* top = m->find_instance("202");
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top->instance_id(), "202");
    EXPECT_EQ(top->label(), "WindowServer");
    EXPECT_NEAR(top->value().number, 12.0, kEps);
    ASSERT_TRUE(top->value().valid);
    ASSERT_TRUE(top->value().text.has_value());
    EXPECT_EQ(*top->value().text, "sleeping");  // 狀態 surface 為文字

    const auto* second = m->find_instance("101");
    ASSERT_NE(second, nullptr);
    EXPECT_NEAR(second->value().number, 1.5, kEps);
    EXPECT_EQ(*second->value().text, "running");

    // processes() 回排序後的完整五欄位（含未經排序維度的記憶體）。
    ASSERT_EQ(p.processes().size(), 2u);
    EXPECT_EQ(p.processes()[0].pid, 202u);
    EXPECT_EQ(p.processes()[0].memory_bytes, 300u * 1024 * 1024);
    EXPECT_EQ(p.processes()[1].pid, 101u);
}

// ===========================================================================
// top-N 依 CPU 排序
// ===========================================================================
TEST(ProcessMonitorProvider, TopNByCpu) {
    auto src = makeSource({
        proc(1, "a", 5.0, 100),
        proc(2, "b", 50.0, 100),
        proc(3, "c", 20.0, 100),
        proc(4, "d", 80.0, 100),
    });
    ProcessMonitorProvider p{src, ProcessSortKey::Cpu, /*top_n=*/2};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("proc.cpu");
    ASSERT_NE(m, nullptr);

    // top-2 依 CPU：d(80) 與 b(50)。
    EXPECT_EQ(p.process_count(), 2u);
    EXPECT_EQ(m->instance_count(), 2u);
    ASSERT_EQ(p.processes().size(), 2u);
    EXPECT_EQ(p.processes()[0].name, "d");
    EXPECT_NEAR(p.processes()[0].cpu_percent, 80.0, kEps);
    EXPECT_EQ(p.processes()[1].name, "b");
    // 未進 top-2 的 a / c 不成為實例。
    EXPECT_EQ(m->find_instance("1"), nullptr);
    EXPECT_EQ(m->find_instance("3"), nullptr);
    EXPECT_NE(m->find_instance("4"), nullptr);
}

// ===========================================================================
// top-N 依記憶體排序（+ bytes→MB 換算 + range at_least(0)）
// ===========================================================================
TEST(ProcessMonitorProvider, TopNByMemoryWithMbConversion) {
    const std::uint64_t mb = 1024u * 1024u;
    auto src = makeSource({
        proc(1, "small", 0.0, 10 * mb),
        proc(2, "big", 0.0, 512 * mb),
        proc(3, "mid", 0.0, 128 * mb),
    });
    ProcessMonitorProvider p{src, ProcessSortKey::Memory, /*top_n=*/2};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("proc.memory");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), "MB");

    // top-2 依記憶體：big(512MB)、mid(128MB)。值以 MB 暴露。
    ASSERT_EQ(p.processes().size(), 2u);
    EXPECT_EQ(p.processes()[0].name, "big");
    const auto* big = m->find_instance("2");
    ASSERT_NE(big, nullptr);
    EXPECT_NEAR(big->value().number, 512.0, kEps);  // 512 MiB → 512 MB
    const auto* mid = m->find_instance("3");
    ASSERT_NE(mid, nullptr);
    EXPECT_NEAR(mid->value().number, 128.0, kEps);

    // 範圍 at_least(0)：下界 0、上無界。
    auto r = m->range();
    EXPECT_TRUE(r.has_min());
    EXPECT_FALSE(r.has_max());
    EXPECT_NEAR(*r.min, 0.0, kEps);
}

// 排序平手以 pid 遞增決定性。
TEST(TopProcesses, TieBrokenByPidAscending) {
    std::vector<ProcessInfo> procs = {
        proc(30, "c", 10.0, 100),
        proc(10, "a", 10.0, 100),
        proc(20, "b", 10.0, 100),
    };
    auto out = top_processes(procs, ProcessSortKey::Cpu, /*top_n=*/0);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].pid, 10u);  // 同 CPU → pid 小在前
    EXPECT_EQ(out[1].pid, 20u);
    EXPECT_EQ(out[2].pid, 30u);
}

// top_n==0 → 全部；top_n 大於行程數 → 全部（不崩）。
TEST(TopProcesses, ZeroMeansAllAndOverLimitClamps) {
    std::vector<ProcessInfo> procs = {proc(1, "a", 5.0, 100), proc(2, "b", 9.0, 100)};
    EXPECT_EQ(top_processes(procs, ProcessSortKey::Cpu, 0).size(), 2u);
    EXPECT_EQ(top_processes(procs, ProcessSortKey::Cpu, 10).size(), 2u);
    EXPECT_EQ(top_processes(procs, ProcessSortKey::Cpu, 1).size(), 1u);
    EXPECT_EQ(top_processes({}, ProcessSortKey::Cpu, 3).size(), 0u);  // 空輸入
}

TEST(ProcessSortValue, CpuAndMemoryDimensions) {
    ProcessInfo x = proc(1, "x", 33.0, 4096);
    EXPECT_NEAR(process_sort_value(x, ProcessSortKey::Cpu), 33.0, kEps);
    EXPECT_NEAR(process_sort_value(x, ProcessSortKey::Memory), 4096.0, kEps);
}

// ===========================================================================
// 行程數變動（增 / 減）
// ===========================================================================
TEST(ProcessMonitorProvider, ProcessCountGrowsAcrossSamples) {
    auto src = std::make_shared<NullProcessSource>();
    src->set_sequence({
        {proc(1, "a", 5.0, 100)},                         // t0：1 行程
        {proc(1, "a", 6.0, 100), proc(2, "b", 9.0, 100)},  // t1：2 行程（b 上線）
    });
    ProcessMonitorProvider p{src, ProcessSortKey::Cpu};  // 全部
    MetricRegistry reg;
    reg.add_provider(p);  // register 消耗 t0 → 1 實例
    auto m = reg.get("proc.cpu");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(p.process_count(), 1u);

    p.sample();  // t1 → 新增 b
    EXPECT_EQ(p.process_count(), 2u);
    EXPECT_EQ(m->instance_count(), 2u);
    // 既有 a 值更新為 6%、新 b 為 9%。
    EXPECT_NEAR(m->find_instance("1")->value().number, 6.0, kEps);
    ASSERT_NE(m->find_instance("2"), nullptr);
    EXPECT_NEAR(m->find_instance("2")->value().number, 9.0, kEps);
}

TEST(ProcessMonitorProvider, DepartedProcessMarkedUnknownNotRemoved) {
    auto src = std::make_shared<NullProcessSource>();
    src->set_sequence({
        {proc(1, "a", 5.0, 100), proc(2, "b", 9.0, 100)},  // t0：兩行程
        {proc(1, "a", 7.0, 100)},                          // t1：b 結束
    });
    ProcessMonitorProvider p{src, ProcessSortKey::Cpu};
    MetricRegistry reg;
    reg.add_provider(p);  // t0：2 實例
    auto m = reg.get("proc.cpu");
    EXPECT_EQ(p.process_count(), 2u);

    p.sample();  // t1：b 缺席 → 設未知，實例不縮減（誠實表達）。
    EXPECT_EQ(p.process_count(), 2u);  // 不縮減
    EXPECT_TRUE(m->find_instance("1")->value().valid);
    EXPECT_NEAR(m->find_instance("1")->value().number, 7.0, kEps);
    EXPECT_FALSE(m->find_instance("2")->value().valid);  // 缺席 → 未知
    // processes()（本次 top-N）只含存活的 a。
    ASSERT_EQ(p.processes().size(), 1u);
    EXPECT_EQ(p.processes()[0].pid, 1u);
}

// ===========================================================================
// 無讀值誠實 invalid
// ===========================================================================
TEST(ProcessMonitorProvider, NoReadingIsInvalid) {
    // 預設 null 來源（未注入）→ 無讀值。
    auto src = std::make_shared<NullProcessSource>();
    ProcessMonitorProvider p{src, ProcessSortKey::Cpu};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("proc.cpu");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->instance_count(), 0u);  // 無讀值 → 無實例
    EXPECT_EQ(p.process_count(), 0u);
    EXPECT_FALSE(p.has_reading());

    // 之後注入資料 → sample() 後有讀值。
    src->set_processes({proc(5, "x", 3.0, 100)});
    p.sample();
    EXPECT_TRUE(p.has_reading());
    EXPECT_EQ(p.process_count(), 1u);
    EXPECT_TRUE(m->find_instance("5")->value().valid);

    // 再回無讀值 → 既有實例設為未知、不崩。
    src->clear();
    p.sample();
    EXPECT_FALSE(p.has_reading());
    EXPECT_FALSE(m->find_instance("5")->value().valid);
}

// ===========================================================================
// 經 E2-02 頻率採樣（除頻排程）
// ===========================================================================
TEST(ProcessMonitorProvider, SampledViaE2_02Scheduler) {
    // 一列快照：單行程 CPU 每份遞增（→ 歷史鋪成序列）。
    auto src = std::make_shared<NullProcessSource>();
    std::vector<std::vector<ProcessInfo>> seq;
    for (int k = 0; k <= 20; ++k) {
        seq.push_back({proc(1, "a", static_cast<double>(k), 100)});
    }
    src->set_sequence(std::move(seq));
    ProcessMonitorProvider p{src, ProcessSortKey::Cpu};
    MetricRegistry reg;
    reg.add_provider(p);  // register 消耗第 0 份（CPU=0）

    SamplingScheduler sched;
    sched.add_demand(p.metric_id(), p.sampling_tier());  // Normal

    // Normal 預設間隔=8：推進到 tick 8 / 16 各採一次。
    int sampled = 0;
    for (ds::metrics::Tick t = 1; t <= 16; ++t) {
        auto due = sched.advance(t);
        for (const auto& id : due) {
            if (id == p.metric_id()) {
                p.sample();
                ++sampled;
            }
        }
    }
    EXPECT_EQ(sampled, 2);  // tick 8、16

    auto m = reg.get("proc.cpu");
    const auto& hist = m->find_instance("1")->history();
    // register(1 筆 CPU=0) + 2 次採樣 = 3 筆歷史。
    EXPECT_EQ(hist.size(), 3u);
}

TEST(ProcessMonitorProvider, DeFrequencyCoalescesDemands) {
    ProcessMonitorProvider p{makeSource({proc(1, "a", 5.0, 100)}), ProcessSortKey::Cpu};
    SamplingScheduler sched;
    auto d_low = sched.add_demand(p.metric_id(), SamplingTier::Low);
    sched.add_demand(p.metric_id(), SamplingTier::High);
    ASSERT_TRUE(sched.effective_tier(p.metric_id()).has_value());
    EXPECT_EQ(*sched.effective_tier(p.metric_id()), SamplingTier::High);  // 最高頻者
    EXPECT_EQ(sched.demand_count(p.metric_id()), 2u);
    EXPECT_TRUE(sched.remove_demand(d_low));
    EXPECT_EQ(*sched.effective_tier(p.metric_id()), SamplingTier::High);
}

// ===========================================================================
// null 來源行為
// ===========================================================================
TEST(ProcessMonitorProvider, NullSourcePointerIsConservative) {
    ProcessMonitorProvider p{nullptr, ProcessSortKey::Memory};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p), 1u);  // 仍掛上指標
    auto m = reg.get("proc.memory");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->instance_count(), 0u);
    EXPECT_FALSE(p.has_reading());
    p.sample();  // 不崩
    EXPECT_EQ(p.process_count(), 0u);
}

TEST(ProcessMonitorProvider, SampleBeforeRegisterIsNoop) {
    ProcessMonitorProvider p{makeSource({proc(1, "a", 5.0, 100)})};
    p.sample();  // 尚未 register → no-op、不崩
    EXPECT_EQ(p.process_count(), 0u);
}

// NullProcessSource 序列語意：列盡回最後一份、空序列回 unknown。
TEST(NullProcessSource, SequenceExhaustionAndEmpty) {
    NullProcessSource src;
    EXPECT_FALSE(src.sample().valid);  // 空 → unknown

    src.set_sequence({{proc(1, "a", 1.0, 100)}, {proc(1, "a", 2.0, 100)}});
    EXPECT_NEAR(src.sample().processes[0].cpu_percent, 1.0, kEps);
    EXPECT_NEAR(src.sample().processes[0].cpu_percent, 2.0, kEps);
    // 列盡 → 持續回最後一份。
    EXPECT_NEAR(src.sample().processes[0].cpu_percent, 2.0, kEps);

    src.clear();
    EXPECT_FALSE(src.sample().valid);  // 清空回 unknown
}

// ===========================================================================
// 範圍 / 消費者範式 / 重複註冊 / 值模型
// ===========================================================================
TEST(ProcessMonitorProvider, CpuRangeBoundedZeroToHundred) {
    ProcessMonitorProvider p{makeSource({proc(1, "a", 50.0, 100)}), ProcessSortKey::Cpu};
    MetricRegistry reg;
    reg.add_provider(p);
    auto m = reg.get("proc.cpu");
    auto r = m->range();
    ASSERT_TRUE(r.is_bounded());
    EXPECT_NEAR(*r.min, 0.0, kEps);
    EXPECT_NEAR(*r.max, 100.0, kEps);
    auto norm = r.normalized(50.0);
    ASSERT_TRUE(norm.has_value());
    EXPECT_NEAR(*norm, 0.5, kEps);
}

// 掛件風格消費者：只透過 E2-01 registry / Metric 抽象走訪，完全不觸及具體型別。
TEST(ProcessMonitorProvider, ConsumerUsesOnlyE2_01Abstractions) {
    auto src = makeSource({proc(1, "a", 20.0, 100), proc(2, "b", 30.0, 100)});
    ProcessMonitorProvider p{src, ProcessSortKey::Cpu};
    MetricRegistry reg;
    reg.add_provider(p);

    std::shared_ptr<Metric> m = reg.get("proc.cpu");
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), "%");
    double sum = 0.0;
    for (std::size_t i = 0; i < m->instance_count(); ++i) {
        const auto& inst = m->instance(i);
        if (inst.value().valid) sum += inst.value().number;
    }
    EXPECT_NEAR(sum, 50.0, kEps);  // 20 + 30
}

TEST(ProcessMonitorProvider, DuplicateRegistrationRejected) {
    ProcessMonitorProvider p1{makeSource({proc(1, "a", 5.0, 100)}), ProcessSortKey::Cpu};
    ProcessMonitorProvider p2{makeSource({proc(2, "b", 9.0, 100)}), ProcessSortKey::Cpu};
    MetricRegistry reg;
    EXPECT_EQ(reg.add_provider(p1), 1u);
    EXPECT_EQ(reg.add_provider(p2), 0u);  // 同 id → 保守拒絕
    EXPECT_EQ(reg.size(), 1u);
}

TEST(ProcessStatus, ToStringStable) {
    EXPECT_EQ(std::string(to_string(ProcessStatus::Running)), "running");
    EXPECT_EQ(std::string(to_string(ProcessStatus::Sleeping)), "sleeping");
    EXPECT_EQ(std::string(to_string(ProcessStatus::Stopped)), "stopped");
    EXPECT_EQ(std::string(to_string(ProcessStatus::Zombie)), "zombie");
    EXPECT_EQ(std::string(to_string(ProcessStatus::Unknown)), "unknown");
}

TEST(ProcessSample, UnknownDefaultAndEquality) {
    ProcessSample u = ProcessSample::unknown();
    EXPECT_FALSE(u.valid);
    EXPECT_TRUE(u.empty());
    ProcessSample a;
    a.valid = true;
    a.processes = {proc(1, "a", 5.0, 100)};
    ProcessSample b = a;
    EXPECT_EQ(a, b);
    b.processes[0].cpu_percent = 6.0;
    EXPECT_NE(a, b);
}

TEST(ProcessInfo, Equality) {
    ProcessInfo a = proc(1, "a", 5.0, 100, ProcessStatus::Running);
    ProcessInfo b = a;
    EXPECT_EQ(a, b);
    b.status = ProcessStatus::Zombie;
    EXPECT_NE(a, b);
}
