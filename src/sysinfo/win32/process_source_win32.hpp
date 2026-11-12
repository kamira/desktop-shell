// W1-08 win32 行程列舉後端 — 其二：真實 win32 列舉
//
// 實作 E2-19 宣告的 `ds::sysinfo::ProcessSource` 抽象（上游，可讀不可改）。E2-19 的
// `ProcessMonitorProvider`（top-N 排序、指標實例、歷史環）與 C2-11 widget **一行不動**，
// 只是建構時注入本類而非 `NullProcessSource` —— 這正是「平台差異靠換實作、不靠 `#ifdef`」
// 的落地方式（驗收標準：契約測試不得含平台分支）。
//
//   ProcessSource::sample()  → EnumProcesses + 逐 pid OpenProcess
//   pid                      → EnumProcesses 回傳的 DWORD
//   name                     → QueryFullProcessImageNameW 取基本名（UTF-16 → UTF-8）
//   cpu_percent              → GetProcessTimes(kernel+user) 的**兩次取樣差分** / 經過時間 / 核心數
//   memory_bytes             → GetProcessMemoryInfo().WorkingSetSize
//   status                   → GetExitCodeProcess：STILL_ACTIVE → Running、否則 Zombie
//
// **本類只負責「取原始讀數」**；換算與「什麼情況算無讀值」的政策全部委派給
// process_policy.hpp 的 `ProcessDeltaTracker`（不碰 win32、在 ubuntu CI 與開發機上真的被
// 測試）。本檔薄到可以用肉眼審，是刻意的：它是唯一無法在非 Windows 上建置的部分。
//
// ---------------------------------------------------------------------------
// 兩個不是 bug 的行為，呼叫端必須知道
// ---------------------------------------------------------------------------
// 1. **第一次 `sample()` 一定回 `ProcessSample::unknown()`。** CPU% 是差分式的，
//    單一快照無從得知使用率（K-002 的解法原文：首次呼叫只建立基準點）。
//    呼叫端（E2-02 排程器）第二次採集起才會拿到讀值。
// 2. **單一 pid 開不起來是常態**，不是錯誤：Idle(0) / System(4) / 受保護行程 / 剛結束的
//    行程都會失敗。這些 pid 被跳過並計入 `skipped_in_last_sample()`，**整份快照仍 valid**。
//    只有「列舉本身失敗」或「可用筆數 0」才是整份無讀值 —— 兩者語意完全不同。
#ifndef DS_SRC_SYSINFO_WIN32_PROCESS_SOURCE_WIN32_HPP
#define DS_SRC_SYSINFO_WIN32_PROCESS_SOURCE_WIN32_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <cstddef>
#include <string>

#include "process_monitor.hpp"  // E2-19（上游，可讀不可改）：ProcessSource / ProcessSample
#include "process_policy.hpp"   // 本單元其一：RawSnapshot / ProcessDeltaTracker

namespace ds::sysinfo::win32 {

// UTF-16 → UTF-8（WideCharToMultiByte CP_UTF8）。失敗回空字串。
//
// 抽成自由函式而非私有方法是為了**可測試性**：它不需要列舉任何行程，可以直接餵入含
// 非 ASCII 的路徑斷言結果（本庫原始碼一律 UTF-8，見根 CMakeLists 的 /utf-8 與 K-001）。
std::string utf8_from_wide(const wchar_t* text, std::size_t length);

// 取路徑的基本名（最後一個 `\` 或 `/` 之後）。純字串操作，可獨立測試。
std::string basename_of(const std::string& path);

// ---------------------------------------------------------------------------
// Win32ProcessSource：E2-19 ProcessSource 的 win32 實作
// ---------------------------------------------------------------------------
class Win32ProcessSource final : public ds::sysinfo::ProcessSource {
public:
    Win32ProcessSource() = default;

    // 取一份快照：capture_raw() → ProcessDeltaTracker::accept()。
    // 無讀值時回 `ProcessSample::unknown()`，原因留在 last_outcome() / last_error()。
    ProcessSample sample() override;

    // --- 以下為 win32 專屬擴充，**不屬於 ProcessSource 契約** ---
    // 跨平台的上層邏輯不碰它們；碰的程式碼本來就只能在 Windows 上跑。

    // 取一次原始讀數（不經差分換算、不更新基準點）。
    // 公開的理由是可測試性：Windows 上的測試可以單獨斷言「列舉成功」「自己的 pid 在裡面」
    // 「名稱非空」，而不必先湊出兩次取樣。
    RawSnapshot capture_raw() const;

    // 最近一次 sample() 的具名結果 / 人類可讀原因 / 被跳過的筆數。
    SampleOutcome last_outcome() const noexcept { return tracker_.last_outcome(); }
    const std::string& last_error() const noexcept { return tracker_.last_error(); }
    std::size_t skipped_in_last_sample() const noexcept {
        return tracker_.skipped_in_last_sample();
    }

    // 目前持有基準點的 pid 數（首次 sample() 之後才非 0）。
    std::size_t tracked_count() const noexcept { return tracker_.tracked_count(); }

    // 丟棄基準點（回到「首次取樣」狀態；下一次 sample() 必然回無讀值）。
    void reset() noexcept { tracker_.reset(); }

private:
    ProcessDeltaTracker tracker_;
};

}  // namespace ds::sysinfo::win32

#endif  // DS_SRC_SYSINFO_WIN32_PROCESS_SOURCE_WIN32_HPP
