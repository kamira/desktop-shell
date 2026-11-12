// W1-08 win32 行程列舉後端 — 其二（真實 win32 列舉）的 gtest
//
// **僅 Windows 建置**（src/sysinfo/win32/CMakeLists.txt 的 `if(WIN32)` 段落）。
// 可重跑證據：
//   cmake -S . -B build -A x64 && cmake --build build --config Debug
//   ctest --test-dir build -C Debug -R '^process_win32$'
//
// 本檔的證據來源是 CI 的 `gate_windows`（windows-latest + MSVC 全建 + ctest）；
// 開發容器是 Linux、無交叉編譯器，**本檔在開發機上從未被建置或執行**，不得宣稱相反。
//
// 分工：政策與換算（K-002 的邊界、差分 CPU%、pid 重用）已由平台中立的
// tests/e2/test_process_win32_policy.cpp 窮舉過，那份在 ubuntu 與 Windows 都跑。
// 本檔只驗「win32 那一步真的讀到了東西」—— 也就是唯一無法在別處驗證的部分。
//
// 不寫「呼叫沒崩潰」式的假斷言充數：每個案例都斷言一個具體的、可能為假的事實。
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "metric.hpp"
#include "process_monitor.hpp"
#include "process_source_win32.hpp"

using ds::metrics::Metric;
using ds::metrics::MetricRegistry;
using ds::sysinfo::ProcessInfo;
using ds::sysinfo::ProcessMonitorProvider;
using ds::sysinfo::ProcessSample;
using ds::sysinfo::ProcessSortKey;
using ds::sysinfo::ProcessSource;
using ds::sysinfo::ProcessStatus;
using ds::sysinfo::win32::basename_of;
using ds::sysinfo::win32::RawSnapshot;
using ds::sysinfo::win32::SampleOutcome;
using ds::sysinfo::win32::utf8_from_wide;
using ds::sysinfo::win32::Win32ProcessSource;

namespace {

// 取樣間隔：差分式 API 的兩次取樣之間必須真的經過時間（K-002 第 1 層）。
// 系統時鐘約 15.6ms 一跳，250ms 遠大於該粒度。
constexpr std::chrono::milliseconds kSampleGap{250};

void wait_one_interval() { std::this_thread::sleep_for(kSampleGap); }

// 燒掉約 kSampleGap 的 CPU 時間（讓本行程的 CPU% 必然 > 0）。
// 以時間為終止條件而非固定迭代數：在快的機器上才不會提早結束。
void burn_cpu() {
    const auto deadline = std::chrono::steady_clock::now() + kSampleGap;
    volatile std::uint64_t sink = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        for (int i = 0; i < 100000; ++i) {
            sink = sink + static_cast<std::uint64_t>(i);
        }
    }
    (void)sink;
}

const ProcessInfo* find_pid(const ProcessSample& s, std::uint64_t pid) {
    for (const auto& p : s.processes) {
        if (p.pid == pid) {
            return &p;
        }
    }
    return nullptr;
}

std::uint64_t self_pid() { return static_cast<std::uint64_t>(GetCurrentProcessId()); }

}  // namespace

// ===========================================================================
// 字串工具（純字串操作，與列舉無關）
// ===========================================================================
TEST(Win32ProcessStrings, Utf8FromWideRoundTripsAscii) {
    const wchar_t text[] = L"notepad.exe";
    EXPECT_EQ(utf8_from_wide(text, 11), "notepad.exe");
}

TEST(Win32ProcessStrings, Utf8FromWideHandlesNonAscii) {
    // 本庫原始碼一律 UTF-8，MSVC 以 /utf-8 建置（根 CMakeLists；見 K-001）。
    const wchar_t text[] = L"記事本.exe";
    EXPECT_EQ(utf8_from_wide(text, 7), "記事本.exe");
}

TEST(Win32ProcessStrings, Utf8FromWideIsEmptyForEmptyInput) {
    EXPECT_TRUE(utf8_from_wide(L"x", 0).empty());
    EXPECT_TRUE(utf8_from_wide(nullptr, 3).empty());
}

TEST(Win32ProcessStrings, BasenameTakesLastComponent) {
    EXPECT_EQ(basename_of("C:\\Windows\\System32\\notepad.exe"), "notepad.exe");
    EXPECT_EQ(basename_of("C:/Windows/notepad.exe"), "notepad.exe");
    EXPECT_EQ(basename_of("notepad.exe"), "notepad.exe");
    EXPECT_TRUE(basename_of("C:\\Windows\\").empty());
}

// ===========================================================================
// capture_raw()：win32 層真的讀到東西
// ===========================================================================
TEST(Win32ProcessSourceRaw, EnumeratesProcessesAndReadsProcessorCount) {
    Win32ProcessSource src;
    const RawSnapshot raw = src.capture_raw();

    EXPECT_TRUE(raw.enumerated);
    EXPECT_GT(raw.listed_pids, 0u);
    EXPECT_FALSE(raw.entries.empty());
    EXPECT_GT(raw.processor_count, 0u);
    EXPECT_GT(raw.wall_100ns, 0u);
}

TEST(Win32ProcessSourceRaw, EveryListedPidIsEitherReadOrCounted) {
    // 不變量：列舉到的每個 pid 要嘛讀成一筆、要嘛計入 skipped。
    // 兩邊加起來不等於 listed_pids 表示有 pid 被靜默丟掉了。
    Win32ProcessSource src;
    const RawSnapshot raw = src.capture_raw();
    ASSERT_TRUE(raw.enumerated);
    EXPECT_EQ(raw.entries.size() + raw.skipped, raw.listed_pids);
}

TEST(Win32ProcessSourceRaw, FindsOurOwnProcessWithAllFiveFields) {
    // 我們自己一定開得起來、一定有名字、一定佔著記憶體。
    // 這是「真的讀到了」最硬的證據：若 OpenProcess / QueryFullProcessImageNameW /
    // GetProcessMemoryInfo 任一步寫錯，本案例就會紅。
    Win32ProcessSource src;
    const RawSnapshot raw = src.capture_raw();
    ASSERT_TRUE(raw.enumerated);

    const auto me = self_pid();
    bool found = false;
    for (const auto& e : raw.entries) {
        if (e.pid != me) {
            continue;
        }
        found = true;
        EXPECT_FALSE(e.name.empty());
        EXPECT_GT(e.memory_bytes, 0u);
        EXPECT_GT(e.created_100ns, 0u);
        EXPECT_EQ(e.liveness, ds::sysinfo::win32::RawLiveness::Running);
        break;
    }
    EXPECT_TRUE(found) << "自己的 pid " << me << " 不在列舉結果中";
}

// ===========================================================================
// sample()：E2-19 ProcessSource 契約的實際行為
// ===========================================================================
TEST(Win32ProcessSource, FirstSampleOnlyEstablishesTheBaseline) {
    Win32ProcessSource src;
    const ProcessSample first = src.sample();

    EXPECT_FALSE(first.valid);
    EXPECT_TRUE(first.empty());
    EXPECT_EQ(src.last_outcome(), SampleOutcome::BaselineOnly);
    EXPECT_FALSE(src.last_error().empty());
    EXPECT_GT(src.tracked_count(), 0u);
}

TEST(Win32ProcessSource, SecondSampleHasRealReadings) {
    Win32ProcessSource src;
    ASSERT_FALSE(src.sample().valid);
    wait_one_interval();

    const ProcessSample s = src.sample();
    ASSERT_TRUE(s.valid) << "無讀值，原因：" << ds::sysinfo::win32::to_string(src.last_outcome())
                         << " / " << src.last_error();
    EXPECT_EQ(src.last_outcome(), SampleOutcome::Ok);
    EXPECT_FALSE(s.empty());

    for (const auto& p : s.processes) {
        EXPECT_GE(p.cpu_percent, 0.0);
        EXPECT_LE(p.cpu_percent, 100.0);  // E2-19 宣告的值域
        EXPECT_FALSE(p.name.empty());
        EXPECT_NE(p.status, ProcessStatus::Sleeping);  // win32 行程層級不產生此值
        EXPECT_NE(p.status, ProcessStatus::Stopped);
    }
}

TEST(Win32ProcessSource, OurOwnBusyLoopShowsUpAsNonZeroCpu) {
    // 只斷言「值域合法」會放過整個量表死掉的情形（K-002 第 3 層：0.0 完美滿足 [0,100]）。
    // 故燒掉一段真實 CPU 時間，斷言自己的 CPU% **確實大於 0**。
    Win32ProcessSource src;
    ASSERT_FALSE(src.sample().valid);
    burn_cpu();

    const ProcessSample s = src.sample();
    ASSERT_TRUE(s.valid) << src.last_error();
    const ProcessInfo* me = find_pid(s, self_pid());
    ASSERT_NE(me, nullptr) << "自己的 pid 不在快照中";
    EXPECT_GT(me->cpu_percent, 0.0);
    EXPECT_GT(me->memory_bytes, 0u);
}

TEST(Win32ProcessSource, ProtectedProcessesAreSkippedNotFatal) {
    // 一般使用者權限下，Idle(0) / System(4) / 受保護行程開不起來是常態。
    // 整份快照必須照常有讀值，而被跳過的筆數留在診斷裡。
    Win32ProcessSource src;
    ASSERT_FALSE(src.sample().valid);
    wait_one_interval();

    const ProcessSample s = src.sample();
    ASSERT_TRUE(s.valid) << src.last_error();
    // 不斷言 skipped > 0（權限充足的 runner 上可能真的一個都沒跳過），
    // 但斷言它不會把整份快照拖成無讀值 —— 這才是本案例要守的分界。
    EXPECT_EQ(src.last_outcome(), SampleOutcome::Ok);
}

TEST(Win32ProcessSource, ResetReturnsToTheBaselineOnlyState) {
    Win32ProcessSource src;
    ASSERT_FALSE(src.sample().valid);
    wait_one_interval();
    ASSERT_TRUE(src.sample().valid);

    src.reset();
    EXPECT_EQ(src.tracked_count(), 0u);
    const ProcessSample after = src.sample();
    EXPECT_FALSE(after.valid);
    EXPECT_EQ(src.last_outcome(), SampleOutcome::BaselineOnly);
}

TEST(Win32ProcessSource, IsUsableThroughTheProcessSourceContract) {
    // 呼叫端只看得到 E2-19 的抽象型別。
    Win32ProcessSource src;
    ProcessSource& contract = src;
    EXPECT_FALSE(contract.sample().valid);
    wait_one_interval();
    EXPECT_TRUE(contract.sample().valid);
}

// ===========================================================================
// 與 E2-19 提供者的組裝（E2-19 / E2-01 一行不動）
// ===========================================================================
TEST(Win32ProcessSource, FeedsE2_19ProviderAndE2_01Registry) {
    auto src = std::make_shared<Win32ProcessSource>();
    ProcessMonitorProvider provider{src, ProcessSortKey::Cpu, /*top_n=*/5};
    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);

    // register_metrics() 吃到首次取樣（基準點）→ 誠實地沒有讀值。
    EXPECT_FALSE(provider.has_reading());
    burn_cpu();

    provider.sample();
    ASSERT_TRUE(provider.has_reading()) << src->last_error();
    EXPECT_GT(provider.process_count(), 0u);
    EXPECT_LE(provider.processes().size(), 5u);  // top_n 生效

    const std::shared_ptr<Metric> m = registry.get(ProcessMonitorProvider::kCpuMetricId);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), ProcessMonitorProvider::kCpuUnit);
    EXPECT_GT(m->instance_count(), 0u);

    // top-N 由高到低（E2-19 的 top_processes()；本單元不自己排序）。
    const auto& top = provider.processes();
    for (std::size_t i = 1; i < top.size(); ++i) {
        EXPECT_GE(top[i - 1].cpu_percent, top[i].cpu_percent);
    }
}

TEST(Win32ProcessSource, MemorySortKeyExposesMegabytes) {
    auto src = std::make_shared<Win32ProcessSource>();
    ProcessMonitorProvider provider{src, ProcessSortKey::Memory, /*top_n=*/3};
    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);
    wait_one_interval();
    provider.sample();

    ASSERT_TRUE(provider.has_reading()) << src->last_error();
    const std::shared_ptr<Metric> m = registry.get(ProcessMonitorProvider::kMemoryMetricId);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), ProcessMonitorProvider::kMemoryUnit);

    const auto& top = provider.processes();
    ASSERT_FALSE(top.empty());
    EXPECT_GT(top[0].memory_bytes, 0u);
    for (std::size_t i = 1; i < top.size(); ++i) {
        EXPECT_GE(top[i - 1].memory_bytes, top[i].memory_bytes);
    }
}
