// W1-08 win32 行程列舉後端 — 其一（政策層）的 gtest
//
// 可重跑證據（本機與 CI 皆跑；本檔**不是** Windows 專屬）：
//   cmake -S . -B build && cmake --build build -j && ctest --test-dir build -R '^w1_08_policy$'
//
// 本檔刻意**平台中立**：不含任何平台條件編譯、不碰 win32 API、不 include windows.h。
// 它測的是「原始讀數 → ProcessSample」的換算與政策——也就是本單元最容易出錯、
// 而且一旦錯了會「看起來完全正常」的那一半（K-002）。因此它在 ubuntu 的 G3 與開發機上
// 都真的被建置與執行，不像 win32 那半只有 gate_windows 看得到。
//
// 平台差異在本檔的表達方式是**換一個 ProcessSource 實作注入**（見 ReplaySource），
// 不是 `#ifdef`——這正是驗收標準「契約測試不得含平台分支」的落地方式。
#include "process_policy.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "metric.hpp"
#include "process_monitor.hpp"

using ds::metrics::Metric;
using ds::metrics::MetricRegistry;
using ds::sysinfo::ProcessInfo;
using ds::sysinfo::ProcessMonitorProvider;
using ds::sysinfo::ProcessSample;
using ds::sysinfo::ProcessSortKey;
using ds::sysinfo::ProcessSource;
using ds::sysinfo::ProcessStatus;
using ds::sysinfo::win32::cpu_percent_from_delta;
using ds::sysinfo::win32::kTicksPerSecond;
using ds::sysinfo::win32::ProcessDeltaTracker;
using ds::sysinfo::win32::RawLiveness;
using ds::sysinfo::win32::RawProcessEntry;
using ds::sysinfo::win32::RawSnapshot;
using ds::sysinfo::win32::SampleOutcome;
using ds::sysinfo::win32::status_from_raw;
using ds::sysinfo::win32::Ticks100ns;
using ds::sysinfo::win32::to_string;

namespace {

constexpr double kEps = 1e-9;

// 一筆原始讀數。busy / created 以「秒」給，可讀性遠高於 100ns 刻。
RawProcessEntry raw_entry(std::uint64_t pid, std::string name, double busy_seconds,
                          double created_seconds, std::uint64_t mem_bytes,
                          RawLiveness live = RawLiveness::Running) {
    RawProcessEntry e;
    e.pid = pid;
    e.name = std::move(name);
    e.busy_100ns = static_cast<Ticks100ns>(busy_seconds * static_cast<double>(kTicksPerSecond));
    e.created_100ns =
        static_cast<Ticks100ns>(created_seconds * static_cast<double>(kTicksPerSecond));
    e.memory_bytes = mem_bytes;
    e.liveness = live;
    return e;
}

// 一份成功列舉的原始快照。
RawSnapshot ok_snapshot(double wall_seconds, std::vector<RawProcessEntry> entries,
                        std::uint32_t cpus = 1, std::size_t skipped = 0) {
    RawSnapshot s;
    s.enumerated = true;
    s.wall_100ns = static_cast<Ticks100ns>(wall_seconds * static_cast<double>(kTicksPerSecond));
    s.processor_count = cpus;
    s.skipped = skipped;
    s.listed_pids = entries.size() + skipped;
    s.entries = std::move(entries);
    return s;
}

// 依 pid 找出快照中的某筆（找不到回 nullptr）。
const ProcessInfo* find_pid(const ProcessSample& s, std::uint64_t pid) {
    for (const auto& p : s.processes) {
        if (p.pid == pid) {
            return &p;
        }
    }
    return nullptr;
}

// 一個以**預錄原始快照**驅動的 ProcessSource：結構與 Win32ProcessSource 完全相同
// （capture → ProcessDeltaTracker::accept），只是「取讀數」那一步換成讀預錄資料。
// 用它來證明本層真的滿足 E2-19 的 ProcessSource 契約，而不需要任何平台分支。
class ReplaySource final : public ProcessSource {
public:
    explicit ReplaySource(std::vector<RawSnapshot> script) : script_(std::move(script)) {}

    ProcessSample sample() override {
        if (script_.empty()) {
            return ProcessSample::unknown();
        }
        const RawSnapshot& raw = script_[cursor_ < script_.size() ? cursor_ : script_.size() - 1];
        if (cursor_ < script_.size()) {
            ++cursor_;
        }
        return tracker_.accept(raw);
    }

    SampleOutcome last_outcome() const noexcept { return tracker_.last_outcome(); }

private:
    std::vector<RawSnapshot> script_;
    std::size_t cursor_ = 0;
    ProcessDeltaTracker tracker_;
};

}  // namespace

// ===========================================================================
// 具名列舉的字串（NFR-02 慣例：具名概念要有穩定字串）
// ===========================================================================
TEST(W1_08_Outcome, ToStringIsStableForEveryOutcome) {
    EXPECT_STREQ(to_string(SampleOutcome::Ok), "ok");
    EXPECT_STREQ(to_string(SampleOutcome::BaselineOnly), "baseline_only");
    EXPECT_STREQ(to_string(SampleOutcome::EnumerationFailed), "enumeration_failed");
    EXPECT_STREQ(to_string(SampleOutcome::NoTimeElapsed), "no_time_elapsed");
    EXPECT_STREQ(to_string(SampleOutcome::InvalidCpuCount), "invalid_cpu_count");
    EXPECT_STREQ(to_string(SampleOutcome::NoUsableEntries), "no_usable_entries");
}

// ===========================================================================
// CPU% 的純計算
// ===========================================================================
TEST(W1_08_CpuPercent, HalfOfOneCoreIsFiftyPercent) {
    EXPECT_NEAR(cpu_percent_from_delta(kTicksPerSecond / 2, kTicksPerSecond, 1), 50.0, kEps);
}

TEST(W1_08_CpuPercent, FullCoreIsHundredPercent) {
    EXPECT_NEAR(cpu_percent_from_delta(kTicksPerSecond, kTicksPerSecond, 1), 100.0, kEps);
}

TEST(W1_08_CpuPercent, DivisorIncludesProcessorCount) {
    // 同樣燒掉一整個核心的時間，在 4 核機器上是 25%——除數漏掉核心數會算出 100%，
    // 那是個完全落在合法值域內的錯值（只查值域的驗收抓不到，K-002 第 3 層）。
    EXPECT_NEAR(cpu_percent_from_delta(kTicksPerSecond, kTicksPerSecond, 4), 25.0, kEps);
}

TEST(W1_08_CpuPercent, ClampsMeasurementJitterToContractRange) {
    // 時鐘粒度差異可讓滿載行程算出 >100%：clamp 到契約值域，而不是回報故障。
    EXPECT_NEAR(cpu_percent_from_delta(kTicksPerSecond * 3, kTicksPerSecond, 1), 100.0, kEps);
}

TEST(W1_08_CpuPercent, ZeroBusyIsALegitimateZero) {
    // 0.0 只有在這裡才該出現：真的沒用到 CPU。它是**讀值**，不是失敗值。
    EXPECT_NEAR(cpu_percent_from_delta(0, kTicksPerSecond, 1), 0.0, kEps);
}

TEST(W1_08_CpuPercent, UncomputableReturnsNegativeNotZero) {
    // 失敗值不得與合法值撞號（K-002 的解法原文）。0.0 是合法 CPU%，-1.0 不可能是。
    EXPECT_LT(cpu_percent_from_delta(kTicksPerSecond, 0, 1), 0.0);
    EXPECT_LT(cpu_percent_from_delta(kTicksPerSecond, kTicksPerSecond, 0), 0.0);
}

// ===========================================================================
// 狀態映射
// ===========================================================================
TEST(W1_08_Status, MapsThreeKnownLivenessValues) {
    EXPECT_EQ(status_from_raw(raw_entry(1, "a", 0, 0, 0, RawLiveness::Running)),
              ProcessStatus::Running);
    EXPECT_EQ(status_from_raw(raw_entry(1, "a", 0, 0, 0, RawLiveness::Exited)),
              ProcessStatus::Zombie);
    EXPECT_EQ(status_from_raw(raw_entry(1, "a", 0, 0, 0, RawLiveness::Unknown)),
              ProcessStatus::Unknown);
}

TEST(W1_08_Status, NeverFabricatesSleepingOrStopped) {
    // win32 的**行程**層級沒有 Sleeping / Stopped 語意（那是執行緒層級）。
    // 三種 liveness 窮舉，沒有任何一條路徑會產生這兩個值。
    for (RawLiveness live : {RawLiveness::Running, RawLiveness::Exited, RawLiveness::Unknown}) {
        const ProcessStatus st = status_from_raw(raw_entry(1, "a", 0, 0, 0, live));
        EXPECT_NE(st, ProcessStatus::Sleeping);
        EXPECT_NE(st, ProcessStatus::Stopped);
    }
}

// ===========================================================================
// 首次取樣：只建立基準點
// ===========================================================================
TEST(W1_08_Tracker, FirstSampleIsBaselineOnlyAndUnknown) {
    ProcessDeltaTracker t;
    EXPECT_FALSE(t.has_baseline());

    const ProcessSample s = t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}));

    EXPECT_FALSE(s.valid);
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s, ProcessSample::unknown());
    EXPECT_EQ(t.last_outcome(), SampleOutcome::BaselineOnly);
    EXPECT_FALSE(t.last_error().empty());  // 不靜默：原因留得下來
    EXPECT_TRUE(t.has_baseline());
    EXPECT_EQ(t.tracked_count(), 1u);
}

TEST(W1_08_Tracker, SecondSampleYieldsRealReadings) {
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096),
                                raw_entry(200, "b.exe", 2.0, 0.0, 8192)},
                         /*cpus=*/2));
    // 2 秒後：100 燒了 1 秒 CPU、200 燒了 0.2 秒；2 核 → 容量 4 秒。
    const ProcessSample s =
        t.accept(ok_snapshot(12.0, {raw_entry(100, "a.exe", 2.0, 0.0, 5000),
                                    raw_entry(200, "b.exe", 2.2, 0.0, 9000)},
                             /*cpus=*/2));

    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_EQ(t.last_outcome(), SampleOutcome::Ok);
    EXPECT_TRUE(t.last_error().empty());

    const ProcessInfo* a = find_pid(s, 100);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->name, "a.exe");
    EXPECT_NEAR(a->cpu_percent, 25.0, 1e-6);  // 1s / (2s * 2 核)
    EXPECT_EQ(a->memory_bytes, 5000u);
    EXPECT_EQ(a->status, ProcessStatus::Running);

    const ProcessInfo* b = find_pid(s, 200);
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->cpu_percent, 5.0, 1e-6);  // 0.2s / 4s
}

TEST(W1_08_Tracker, IdleProcessReportsZeroAsARealReading) {
    // 這是唯一應該看到 0.0 的情形：取樣有效、該行程真的沒用 CPU。
    // 與「無讀值」在呼叫端眼中必須可分辨——後者 valid==false。
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "idle.exe", 5.0, 0.0, 1024)}));
    const ProcessSample s =
        t.accept(ok_snapshot(11.0, {raw_entry(100, "idle.exe", 5.0, 0.0, 1024)}));

    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.size(), 1u);
    EXPECT_NEAR(s.processes[0].cpu_percent, 0.0, kEps);
}

// ===========================================================================
// 整份無讀值的四種成因（各自具名，不混為一談）
// ===========================================================================
TEST(W1_08_Tracker, EnumerationFailureIsUnknownNotAnEmptyProcessList) {
    // K-002 的核心：列舉失敗 ≠ 沒有行程。回空清單 + valid==true 會讓「權限不足」
    // 看起來像「這台機器沒有任何行程」，而那在呼叫端是個完全合法的狀態。
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}));

    RawSnapshot failed;  // enumerated == false
    failed.wall_100ns = 12 * kTicksPerSecond;
    failed.processor_count = 1;
    const ProcessSample s = t.accept(failed);

    EXPECT_FALSE(s.valid);
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(t.last_outcome(), SampleOutcome::EnumerationFailed);
    EXPECT_FALSE(t.last_error().empty());
}

TEST(W1_08_Tracker, EnumerationFailureKeepsTheBaseline) {
    // 基準點保留 → 下一輪仍能差分，只是區間較長（那仍是真實區間的平均，不是假值）。
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}));
    EXPECT_EQ(t.tracked_count(), 1u);

    RawSnapshot failed;
    failed.wall_100ns = 11 * kTicksPerSecond;
    failed.processor_count = 1;
    t.accept(failed);
    EXPECT_EQ(t.tracked_count(), 1u);  // 沒被清掉

    // 從 10s 到 12s 共 2 秒、燒了 1 秒 → 50%（而非從 11s 起算的 100%）。
    const ProcessSample s =
        t.accept(ok_snapshot(12.0, {raw_entry(100, "a.exe", 2.0, 0.0, 4096)}));
    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.size(), 1u);
    EXPECT_NEAR(s.processes[0].cpu_percent, 50.0, 1e-6);
}

TEST(W1_08_Tracker, ZeroElapsedTimeIsUnknown) {
    // K-002 第 1 層：差分式 API 的兩次取樣之間必須真的經過時間，否則差分恆為 0——
    // 而 0 是合法的 CPU%，整個量表會死掉卻看起來「很閒」。
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}));
    const ProcessSample s =
        t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}));

    EXPECT_FALSE(s.valid);
    EXPECT_EQ(t.last_outcome(), SampleOutcome::NoTimeElapsed);
}

TEST(W1_08_Tracker, ZeroElapsedTimeDoesNotAdvanceTheBaseline) {
    // 不推進基準點，下一輪才拿得到一個較長但合法的區間（而不是白白損失兩輪）。
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}));
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.5, 0.0, 4096)}));

    const ProcessSample s =
        t.accept(ok_snapshot(12.0, {raw_entry(100, "a.exe", 2.0, 0.0, 4096)}));
    ASSERT_TRUE(s.valid);
    // 以 10s / busy=1.0 為基準 → (2.0-1.0) / 2s = 50%。
    EXPECT_NEAR(s.processes[0].cpu_percent, 50.0, 1e-6);
}

TEST(W1_08_Tracker, UnreadableProcessorCountIsUnknown) {
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}));

    RawSnapshot s = ok_snapshot(12.0, {raw_entry(100, "a.exe", 2.0, 0.0, 4096)});
    s.processor_count = 0;  // GetActiveProcessorCount 失敗
    const ProcessSample out = t.accept(s);

    EXPECT_FALSE(out.valid);
    EXPECT_EQ(t.last_outcome(), SampleOutcome::InvalidCpuCount);
}

TEST(W1_08_Tracker, EnumeratedButNothingReadableIsUnknown) {
    // 機器上至少有「我們自己」這個行程。列舉成功卻 0 筆可用，必然是讀取側壞了
    // （權限不足 / 全數開不起來），絕不是「沒有任何行程」。
    ProcessDeltaTracker t;
    const ProcessSample s = t.accept(ok_snapshot(10.0, {}, /*cpus=*/1, /*skipped=*/312));

    EXPECT_FALSE(s.valid);
    EXPECT_EQ(t.last_outcome(), SampleOutcome::NoUsableEntries);
    EXPECT_FALSE(t.has_baseline());  // 這種快照不足以當基準點
    EXPECT_NE(t.last_error().find("312"), std::string::npos);  // 原因帶得出數字
}

// ===========================================================================
// 單一 pid 失敗 vs 整份失敗（本單元的核心分界）
// ===========================================================================
TEST(W1_08_Tracker, ProtectedProcessesAreSkippedWithoutPoisoningTheSnapshot) {
    // 受保護行程（Idle / System / 防毒）開不起來是**常態**。整份快照必須照常 valid，
    // 否則行程清單在一般使用者權限下永遠是未知的。
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}, 1, /*skipped=*/7));
    const ProcessSample s =
        t.accept(ok_snapshot(12.0, {raw_entry(100, "a.exe", 2.0, 0.0, 4096)}, 1, /*skipped=*/9));

    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.size(), 1u);
    EXPECT_EQ(t.last_outcome(), SampleOutcome::Ok);
    EXPECT_EQ(t.skipped_in_last_sample(), 9u);  // 診斷留得下來，但不是錯誤
}

// ===========================================================================
// 行程起 / 停、pid 重用
// ===========================================================================
TEST(W1_08_Tracker, NewPidIsDeferredOneRoundThenReported) {
    // 新行程在本輪沒有基準點，算不出區間 CPU%。拿「行程開始至今的平均」充數會印出一個
    // 看似合理的數字，比回 0 更難察覺（K-002 附記）——故本輪不回報它。
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}));

    const ProcessSample s2 = t.accept(ok_snapshot(
        12.0, {raw_entry(100, "a.exe", 2.0, 0.0, 4096), raw_entry(300, "new.exe", 9.0, 11.0, 2048)}));
    ASSERT_TRUE(s2.valid);
    EXPECT_EQ(s2.size(), 1u);
    EXPECT_EQ(find_pid(s2, 300), nullptr);
    EXPECT_EQ(t.skipped_in_last_sample(), 1u);

    const ProcessSample s3 = t.accept(ok_snapshot(
        14.0, {raw_entry(100, "a.exe", 2.0, 0.0, 4096), raw_entry(300, "new.exe", 10.0, 11.0, 2048)}));
    ASSERT_TRUE(s3.valid);
    ASSERT_EQ(s3.size(), 2u);
    const ProcessInfo* n = find_pid(s3, 300);
    ASSERT_NE(n, nullptr);
    EXPECT_NEAR(n->cpu_percent, 50.0, 1e-6);  // 2 秒內燒 1 秒
}

TEST(W1_08_Tracker, ReusedPidIsDetectedByCreationTime) {
    // pid 可被重用。只比對 pid 會拿另一個行程的累計時間去差分，算出憑空的使用率
    // （舊行程燒了 100 秒、新行程才剛開始 → busy 倒退或暴跌）。以建立時間判定。
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "old.exe", 50.0, /*created=*/1.0, 4096)}));

    // 同一個 pid，但建立時間不同 = 另一個行程。
    const ProcessSample s2 = t.accept(
        ok_snapshot(12.0, {raw_entry(100, "new.exe", 60.0, /*created=*/11.0, 2048)}));
    EXPECT_FALSE(s2.valid);  // 本輪唯一一筆被延後 → 無可用讀值
    EXPECT_EQ(t.last_outcome(), SampleOutcome::NoUsableEntries);

    // 下一輪就正常了（基準點已換成新行程的）。
    const ProcessSample s3 = t.accept(
        ok_snapshot(14.0, {raw_entry(100, "new.exe", 61.0, /*created=*/11.0, 2048)}));
    ASSERT_TRUE(s3.valid);
    ASSERT_EQ(s3.size(), 1u);
    EXPECT_EQ(s3.processes[0].name, "new.exe");
    EXPECT_NEAR(s3.processes[0].cpu_percent, 50.0, 1e-6);
}

TEST(W1_08_Tracker, BackwardsBusyCounterIsSkippedNotReportedAsZero) {
    // 同一個行程的累計 CPU 時間只會單調增加。倒退表示讀數不可信，
    // 回 0.0 會讓一個壞掉的讀數看起來像「很閒」。
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 5.0, 0.0, 4096),
                                raw_entry(200, "b.exe", 1.0, 0.0, 4096)}));
    const ProcessSample s = t.accept(ok_snapshot(12.0, {raw_entry(100, "a.exe", 3.0, 0.0, 4096),
                                                        raw_entry(200, "b.exe", 2.0, 0.0, 4096)}));
    ASSERT_TRUE(s.valid);
    EXPECT_EQ(s.size(), 1u);
    EXPECT_EQ(find_pid(s, 100), nullptr);  // 倒退的那筆不回報
    EXPECT_NE(find_pid(s, 200), nullptr);
    EXPECT_EQ(t.skipped_in_last_sample(), 1u);
}

TEST(W1_08_Tracker, ExitedProcessLeavesTheBaseline) {
    // 已結束的行程要從基準點移除，否則 pid 重用時留著一個過期的參照。
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096),
                                raw_entry(200, "b.exe", 1.0, 0.0, 4096)}));
    EXPECT_EQ(t.tracked_count(), 2u);

    t.accept(ok_snapshot(12.0, {raw_entry(100, "a.exe", 2.0, 0.0, 4096)}));
    EXPECT_EQ(t.tracked_count(), 1u);
}

TEST(W1_08_Tracker, WholeTableChurnYieldsUnknownNotAnEmptyList) {
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}));
    const ProcessSample s =
        t.accept(ok_snapshot(12.0, {raw_entry(900, "z.exe", 1.0, 11.0, 4096)}));

    EXPECT_FALSE(s.valid);
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(t.last_outcome(), SampleOutcome::NoUsableEntries);
}

TEST(W1_08_Tracker, ZombieStatusSurvivesTheConversion) {
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096, RawLiveness::Running)}));
    const ProcessSample s = t.accept(
        ok_snapshot(12.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096, RawLiveness::Exited)}));

    ASSERT_TRUE(s.valid);
    ASSERT_EQ(s.size(), 1u);
    EXPECT_EQ(s.processes[0].status, ProcessStatus::Zombie);
}

TEST(W1_08_Tracker, ResetReturnsToFirstSampleState) {
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}));
    ASSERT_TRUE(t.has_baseline());

    t.reset();
    EXPECT_FALSE(t.has_baseline());
    EXPECT_EQ(t.tracked_count(), 0u);

    const ProcessSample s =
        t.accept(ok_snapshot(12.0, {raw_entry(100, "a.exe", 2.0, 0.0, 4096)}));
    EXPECT_FALSE(s.valid);
    EXPECT_EQ(t.last_outcome(), SampleOutcome::BaselineOnly);
}

TEST(W1_08_Tracker, ClampsToContractRangeOnJitter) {
    ProcessDeltaTracker t;
    t.accept(ok_snapshot(10.0, {raw_entry(100, "a.exe", 0.0, 0.0, 4096)}));
    // 1 秒內燒了 3 秒 CPU（量測誤差 / 時鐘粒度）→ clamp 到 100。
    const ProcessSample s =
        t.accept(ok_snapshot(11.0, {raw_entry(100, "a.exe", 3.0, 0.0, 4096)}));
    ASSERT_TRUE(s.valid);
    EXPECT_NEAR(s.processes[0].cpu_percent, 100.0, kEps);
    EXPECT_GE(s.processes[0].cpu_percent, 0.0);
    EXPECT_LE(s.processes[0].cpu_percent, 100.0);
}

// ===========================================================================
// E2-19 契約：換實作即可，提供者與消費者一行不動
// ===========================================================================
TEST(W1_08_Contract, PolicyBackedSourceSatisfiesProcessSource) {
    std::vector<RawSnapshot> script;
    script.push_back(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096),
                                        raw_entry(200, "b.exe", 1.0, 0.0, 8192)}));
    script.push_back(ok_snapshot(12.0, {raw_entry(100, "a.exe", 2.0, 0.0, 4096),
                                        raw_entry(200, "b.exe", 1.2, 0.0, 8192)}));
    auto src = std::make_shared<ReplaySource>(std::move(script));

    // 上轉為 E2-19 契約：呼叫端只看得到 ProcessSource。
    ProcessSource& contract = *src;
    const ProcessSample first = contract.sample();
    EXPECT_FALSE(first.valid);  // 首次取樣只建立基準點
    const ProcessSample second = contract.sample();
    ASSERT_TRUE(second.valid);
    EXPECT_EQ(second.size(), 2u);
}

TEST(W1_08_Contract, FeedsE2_19ProviderWithoutTouchingIt) {
    // E2-19 的 ProcessMonitorProvider / E2-01 的 MetricRegistry **一行不動**，
    // 只是注入本單元的來源實作。平台差異靠換 factory / 換注入，不靠 #ifdef。
    std::vector<RawSnapshot> script;
    script.push_back(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096),
                                        raw_entry(200, "b.exe", 1.0, 0.0, 8192)},
                                 /*cpus=*/1));
    script.push_back(ok_snapshot(12.0, {raw_entry(100, "a.exe", 2.0, 0.0, 4096),
                                        raw_entry(200, "b.exe", 1.2, 0.0, 8192)},
                                 /*cpus=*/1));
    auto src = std::make_shared<ReplaySource>(std::move(script));

    ProcessMonitorProvider provider{src, ProcessSortKey::Cpu};
    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);

    // register_metrics() 吃到的是首次取樣（無讀值）→ 誠實地沒有讀值。
    EXPECT_FALSE(provider.has_reading());

    provider.sample();  // 第二次取樣：有讀值
    EXPECT_TRUE(provider.has_reading());
    ASSERT_EQ(provider.process_count(), 2u);

    // 消費者只走 E2-01 抽象，完全不認識本單元的任何型別。
    const std::shared_ptr<Metric> m = registry.get(ProcessMonitorProvider::kCpuMetricId);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->unit(), ProcessMonitorProvider::kCpuUnit);
    ASSERT_EQ(m->instance_count(), 2u);

    // top-N 依 CPU 由高到低：100 燒了 1 秒（50%）、200 燒了 0.2 秒（10%）。
    ASSERT_EQ(provider.processes().size(), 2u);
    EXPECT_EQ(provider.processes()[0].pid, 100u);
    EXPECT_NEAR(provider.processes()[0].cpu_percent, 50.0, 1e-6);
    EXPECT_NEAR(provider.processes()[1].cpu_percent, 10.0, 1e-6);
}

TEST(W1_08_Contract, UnknownSampleReachesTheProviderAsUnknownNotAsZero) {
    // 整條路徑的終點檢查：來源無讀值 → 指標實例是「未知」，不是落在合法值域裡的 0。
    // 這是 K-002 真正要防的那個結果。
    std::vector<RawSnapshot> script;
    script.push_back(ok_snapshot(10.0, {raw_entry(100, "a.exe", 1.0, 0.0, 4096)}));
    script.push_back(ok_snapshot(12.0, {raw_entry(100, "a.exe", 2.0, 0.0, 4096)}));
    RawSnapshot failed;  // enumerated == false
    failed.wall_100ns = 14 * kTicksPerSecond;
    failed.processor_count = 1;
    script.push_back(failed);
    auto src = std::make_shared<ReplaySource>(std::move(script));

    ProcessMonitorProvider provider{src, ProcessSortKey::Cpu};
    MetricRegistry registry;
    ASSERT_EQ(registry.add_provider(provider), 1u);
    provider.sample();  // 有讀值
    ASSERT_TRUE(provider.has_reading());

    provider.sample();  // 列舉失敗
    EXPECT_FALSE(provider.has_reading());

    const std::shared_ptr<Metric> m = registry.get(ProcessMonitorProvider::kCpuMetricId);
    ASSERT_NE(m, nullptr);
    ASSERT_EQ(m->instance_count(), 1u);
    EXPECT_FALSE(m->instance(0).value().valid);
}
