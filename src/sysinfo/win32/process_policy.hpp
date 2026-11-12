// W1-08 win32 行程列舉後端 — 其一：原始讀數 → ProcessSample 的純邏輯層
//
// 本檔**不含任何 win32 呼叫、不 include windows.h、不含平台條件編譯**，因此在
// ubuntu CI（G3）與開發容器上都真的被建置與測試。win32 那一層（process_source_win32.*）
// 只負責把 EnumProcesses / GetProcessTimes / GetProcessMemoryInfo 的讀數填進
// `RawSnapshot`，換算與「什麼情況算無讀值」的政策判斷全部在這裡。
//
// 為什麼要這樣拆（照 H1-01 的先例，CHG-20260803-05-h1_01「版面計算抽成不碰 GDI 的純函式」）：
// 本單元最容易出錯的部分 —— 差分式 CPU% 的除數、pid 重用、「單一 pid 開不起來」與
// 「整份列舉失敗」的區分 —— 全是純算術與政策判斷，不需要 win32。放在這裡它們進得了閘門；
// 留在 win32 檔裡它們的正確性只能靠 windows runner 的整合測試間接推測。
//
// ---------------------------------------------------------------------------
// K-002 是本層的全部重點
// ---------------------------------------------------------------------------
// 知識庫 K-002：用 `0.0` 當取樣失敗值 → 壞掉的指標偽裝成「很閒」，還騙過只查值域的驗收。
// E2-19 的 `ProcessSample` 只有一個 `valid` 旗標，`ProcessInfo` 的五個欄位**沒有** per-field
// 的「無讀值」表示法。所以「讀不到」在本層只有兩種合法表達：
//
//   1. **不回報該筆**（某個 pid 的欄位讀不到 → 跳過它並計數），
//   2. **不回報整份**（`ProcessSample::unknown()`）。
//
// 絕不以 `0.0`、空字串或空清單充數。各情形的判定見 `SampleOutcome` 與 `ProcessDeltaTracker`
// 的註解，每一條都有測試釘住（tests/e2/test_process_win32_policy.cpp）。
#ifndef DS_SRC_SYSINFO_WIN32_PROCESS_POLICY_HPP
#define DS_SRC_SYSINFO_WIN32_PROCESS_POLICY_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "process_monitor.hpp"  // E2-19（上游，可讀不可改）：ProcessSample / ProcessInfo / ProcessStatus

namespace ds::sysinfo::win32 {

// win32 的時間單位：100 奈秒刻（FILETIME / GetProcessTimes 的原生單位）。
// 刻意不換算成秒再傳進來 —— 浮點換算放在邊界上，是「讀數」與「算出來的數」混在一起的開端。
using Ticks100ns = std::uint64_t;

// 一秒有多少個 100ns 刻（診斷 / 測試可讀性用）。
constexpr Ticks100ns kTicksPerSecond = 10'000'000ULL;

// ---------------------------------------------------------------------------
// RawLiveness：win32 層讀到的存活狀態（三態，含「讀不到」）
// ---------------------------------------------------------------------------
// 刻意**不是** bool：`GetExitCodeProcess` 可能失敗，而「讀不到存活狀態」與「已結束」
// 是兩件事。用 bool 就只能挑一個當預設值，那個預設值會變成假讀值。
enum class RawLiveness {
    Unknown,  // 讀不到（保守預設）→ ProcessStatus::Unknown
    Running,  // 仍在執行
    Exited,   // 已結束待回收 → ProcessStatus::Zombie
};

// ---------------------------------------------------------------------------
// RawProcessEntry：單一 pid 的原始讀數（五欄位齊全者才會進 RawSnapshot）
// ---------------------------------------------------------------------------
// 「齊全」的定義在 win32 層：name / busy / created / memory 任一讀不到，該 pid 就不進來，
// 只進 `RawSnapshot::skipped` 的計數。本層看到的每一筆都是真的讀到的值。
// （`liveness` 不在「齊全」要求內：它有誠實的 Unknown 表示法，見 RawLiveness。）
struct RawProcessEntry {
    std::uint64_t pid = 0;
    std::string name;                // 映像檔基本名（已是 UTF-8）
    Ticks100ns busy_100ns = 0;       // kernel + user 累計 CPU 時間
    Ticks100ns created_100ns = 0;    // 行程建立時間：pid 重用的判定依據
    std::uint64_t memory_bytes = 0;  // 常駐（working set）bytes
    RawLiveness liveness = RawLiveness::Unknown;
};

// ---------------------------------------------------------------------------
// RawSnapshot：一次 win32 列舉的完整原始結果
// ---------------------------------------------------------------------------
struct RawSnapshot {
    // 列舉本身是否成功（EnumProcesses 回 FALSE → false）。
    // **這與「列了但一個都讀得到」是兩件完全不同的事**，故獨立一個欄位。
    bool enumerated = false;

    // 取樣時刻（同一時鐘來源的 100ns 刻；差分用，絕對值不具意義）。
    Ticks100ns wall_100ns = 0;

    // 邏輯處理器數（CPU% 的除數之一；0 視為讀不到 → 整份無讀值）。
    std::uint32_t processor_count = 0;

    // EnumProcesses 回報的 pid 總數（含讀不到的）。
    std::size_t listed_pids = 0;

    // 開不起來 / 欄位讀不到而被跳過的 pid 數（受保護行程是常態，見類註）。
    std::size_t skipped = 0;

    // 五欄位齊全的筆數。
    std::vector<RawProcessEntry> entries;
};

// ---------------------------------------------------------------------------
// SampleOutcome：一次換算的具名結果（不靜默吞錯）
// ---------------------------------------------------------------------------
// `unknown()` 有五種成因，混為一談就等於吞錯。呼叫端拿得到具名原因 + `last_error()` 的
// 人類可讀說明，才可能分辨「權限不足」與「機器真的很閒」。
enum class SampleOutcome {
    Ok,                 // 有讀值（至少一筆可用且已有基準點）
    BaselineOnly,       // 首次取樣：只建立基準點，無區間可差分 → unknown
    EnumerationFailed,  // EnumProcesses 失敗 → unknown（列舉失敗 ≠ 沒有行程）
    NoTimeElapsed,      // 兩次取樣之間時間差為 0 → unknown（K-002 第 1 層）
    InvalidCpuCount,    // 處理器數讀不到（0）→ unknown（否則除以 0）
    NoUsableEntries,    // 列舉成功但可用筆數 0 → unknown（至少有「我們自己」，0 筆必是讀取側壞了）
};

// 診斷 / 顯示用穩定字串。
const char* to_string(SampleOutcome outcome) noexcept;

// ---------------------------------------------------------------------------
// CPU% 的純計算
// ---------------------------------------------------------------------------
// `100 * busy_delta / (wall_delta * processor_count)`，clamp 到契約值域 [0,100]。
//
// clamp 的理由（而非回報失敗）：GetProcessTimes 與取樣時刻來自不同的時鐘粒度，
// 滿載行程算出 100.4% 是正常的量測誤差，不是故障。clamp 不會把故障偽裝成正常值 ——
// 故障在本函式之前就已經被擋掉了（wall_delta==0 / cpus==0 由呼叫端判為無讀值）。
//
// `wall_delta == 0` 或 `cpus == 0` 時回 -1.0（**不可能是合法 CPU%**，與合法值域不撞號，
// 正是 K-002 的解法原文）。呼叫端不應走到這裡 —— 它是最後一道防線，不是正常路徑。
double cpu_percent_from_delta(Ticks100ns busy_delta, Ticks100ns wall_delta,
                              std::uint32_t cpus) noexcept;

// Running → Running；Exited → Zombie（已結束待回收）；Unknown → Unknown。
//
// **win32 的行程層級沒有 Sleeping / Stopped 語意**（那是執行緒層級的狀態），故本後端
// 永不產生這兩個值。刻意不去「挑一個最接近的」填上 —— 猜出來的狀態字串會直接出現在
// 使用者眼前，而且看起來完全正常。
ProcessStatus status_from_raw(const RawProcessEntry& entry) noexcept;

// ---------------------------------------------------------------------------
// ProcessDeltaTracker：把連續的 RawSnapshot 換算成 ProcessSample
// ---------------------------------------------------------------------------
// 持有上一次的基準點（每 pid 的累計 busy 時間 + 行程建立時間，以及取樣時刻），
// 因為 CPU% 是**差分式**的：單一快照無從得知使用率。
//
// 政策（每條都有負向測試）：
//   - 首次 `accept()`：只建立基準點，回 unknown（`BaselineOnly`）。
//     K-002 的解法原文就是「首次呼叫只建立基準點並回 -1.0」。
//   - 列舉失敗：回 unknown，**保留**既有基準點（下一輪仍能差分，只是區間較長 ——
//     那仍是一個真實區間的平均，不是假值）。
//   - 時間差為 0 / 處理器數為 0：回 unknown。
//   - 某 pid 本次首見（或建立時間與基準點不同 = **pid 被重用**）：本輪**跳過該筆**，
//     同時寫入基準點，下一輪才回報。沒有基準點就算不出區間 CPU%，而拿「行程開始至今的
//     平均」充數會印出一個看似合理的數字 —— K-002 附記的陷阱。
//   - 列舉成功但可用筆數 0：回 unknown（`NoUsableEntries`）。
//   - 本次未出現的 pid：從基準點移除（行程已結束；留著會在 pid 重用時誤判）。
class ProcessDeltaTracker {
public:
    ProcessDeltaTracker() = default;

    // 換算一份原始快照。見類註的政策表。
    ProcessSample accept(const RawSnapshot& raw);

    // 最近一次 accept() 的具名結果。
    SampleOutcome last_outcome() const noexcept { return last_outcome_; }

    // 最近一次 accept() 為非 Ok 時的人類可讀原因（Ok 時為空字串）。
    const std::string& last_error() const noexcept { return last_error_; }

    // 最近一次 accept() 中被跳過的筆數：win32 層讀不到的（`RawSnapshot::skipped`）
    // 加上本層因「無基準點 / pid 重用」而延後一輪的。受保護行程讓這個數字**常態不為 0**，
    // 它是診斷用的，不是錯誤指標。
    std::size_t skipped_in_last_sample() const noexcept { return skipped_last_; }

    // 目前持有基準點的 pid 數。
    std::size_t tracked_count() const noexcept { return baseline_.size(); }

    // 是否已有基準點（首次取樣前為 false）。
    bool has_baseline() const noexcept { return has_baseline_; }

    // 清除全部基準點（回到「首次取樣」狀態）。
    void reset() noexcept;

private:
    struct Baseline {
        Ticks100ns busy_100ns = 0;
        Ticks100ns created_100ns = 0;
    };

    // 回 unknown 並記下具名原因。
    ProcessSample fail(SampleOutcome outcome, std::string reason);

    std::unordered_map<std::uint64_t, Baseline> baseline_;
    Ticks100ns prev_wall_100ns_ = 0;
    bool has_baseline_ = false;
    SampleOutcome last_outcome_ = SampleOutcome::BaselineOnly;
    std::string last_error_;
    std::size_t skipped_last_ = 0;
};

}  // namespace ds::sysinfo::win32

#endif  // DS_SRC_SYSINFO_WIN32_PROCESS_POLICY_HPP
