// E2-10 時間與日期 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「當前時間／日期」透過 **E2-01 的 MetricProvider 介面**掛成一個指標。
// 這是「新增指標 = 新增 MetricProvider、掛件一行不動」機制的一個具體提供者——它
// **消費 E2-01 契約、不自造指標模型**。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **平台中立、純邏輯**：以 `std::chrono` + 純算術分解時間，不呼叫 OS 時區 / 曆法 API，
//     不含 `#ifdef` / win32 / cocoa / `__APPLE__` 平台分支——換平台一行不動。
//   - **時間來源可注入**：提供者不直接綁 `system_clock::now()`，而是依賴抽象 `TimeSource`。
//     相位 1 / 測試以 `FixedTimeSource` 餵入固定時間點，取樣**決定性**、可完全單元測試。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id    = "time.now"
//   - name  = "Current Time & Date"
//   - unit  = ""（各面向數值維度異質——epoch 秒 / 曆日 / 日內秒——由文字承載表述）
//   - range = unbounded（時間軸無界）
//   - **可列舉實例 = 當前時間的三個面向**（此即單元名「時間與日期」在 E2-01
//     「可列舉實例」要素上的自然對應，與 E2-16「每應用一實例」同精神）：
//       * "epoch" — number = Unix epoch 秒、text = ISO-8601 "YYYY-MM-DDTHH:MM:SSZ"
//       * "date"  — number = 1970-01-01 起的曆日數、text = "YYYY-MM-DD"
//       * "time"  — number = 日內秒（0..86399）、text = "HH:MM:SS"
//     時間點無時序歷史語意，故各實例 history_capacity = 0（誠實表達「無歷史」）。
//     曆法一律以 **UTC** 分解（相位 1 平台中立；本地時區換算涉及 OS，留待後端相位）。
#ifndef DS_MODULES_E2_10_TIME_DATE_HPP
#define DS_MODULES_E2_10_TIME_DATE_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "metric.hpp"  // E2-01 契約（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// CivilTime：一個時間點的 UTC 曆法分解（純值，平台中立）
// ---------------------------------------------------------------------------
// 由 epoch 秒以純算術（Howard Hinnant civil-from-days）分解而得，無任何 OS 呼叫。
struct CivilTime {
    int year = 1970;              // 曆年（可為負，代表西元前）
    unsigned month = 1;           // 月 [1,12]
    unsigned day = 1;             // 日 [1,31]
    unsigned hour = 0;            // 時 [0,23]
    unsigned minute = 0;          // 分 [0,59]
    unsigned second = 0;          // 秒 [0,59]
    std::int64_t epoch_seconds = 0;     // 1970-01-01T00:00:00Z 起的秒數（UTC）
    std::int64_t days_since_epoch = 0;  // 1970-01-01 起的曆日數（floor 除法）
    int second_of_day = 0;              // 日內秒 [0,86399]

    bool operator==(const CivilTime& o) const {
        return year == o.year && month == o.month && day == o.day &&
               hour == o.hour && minute == o.minute && second == o.second &&
               epoch_seconds == o.epoch_seconds &&
               days_since_epoch == o.days_since_epoch &&
               second_of_day == o.second_of_day;
    }
    bool operator!=(const CivilTime& o) const { return !(*this == o); }
};

// 由 Unix epoch 秒分解為 UTC 曆法欄位（純算術，處理負值以 floor 除法）。
CivilTime civil_from_epoch_seconds(std::int64_t epoch_seconds);

// 由時間點分解（取 system_clock 至秒；平台中立）。
CivilTime civil_from_time_point(std::chrono::system_clock::time_point tp);

// 格式化（皆為零外部相依的固定寬度字串）：
std::string format_iso8601(const CivilTime& c);  // "YYYY-MM-DDTHH:MM:SSZ"
std::string format_date(const CivilTime& c);     // "YYYY-MM-DD"
std::string format_clock(const CivilTime& c);    // "HH:MM:SS"

// ---------------------------------------------------------------------------
// TimeSource：可覆寫的時鐘介面（抽象，平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象，**不直接綁 system_clock::now()**——於是測試 / 相位 1 可餵入固定
// 時間點使取樣決定性；真實壁鐘後端（相位 2+）另實作，提供者一行不動。
class TimeSource {
public:
    virtual ~TimeSource() = default;

    // 回傳「現在」的時間點。實作決定其來源（固定注入 / 真實時鐘）。
    virtual std::chrono::system_clock::time_point now() const = 0;

    // 便利：以 epoch 秒表述之「現在」。
    std::int64_t now_epoch_seconds() const {
        return std::chrono::duration_cast<std::chrono::seconds>(now().time_since_epoch())
            .count();
    }

protected:
    TimeSource() = default;
    TimeSource(const TimeSource&) = default;
    TimeSource& operator=(const TimeSource&) = default;
};

// ---------------------------------------------------------------------------
// FixedTimeSource：注入固定時間點的來源（相位 1 預設 / 測試用）
// ---------------------------------------------------------------------------
// **不讀真實時鐘**：回傳注入的固定時間點（預設 = Unix epoch 起點），使取樣決定性。
// 可用 `set_epoch_seconds` / `set_now` 覆寫，模擬「時間前進」而不依賴系統時鐘。
class FixedTimeSource : public TimeSource {
public:
    FixedTimeSource() = default;
    explicit FixedTimeSource(std::chrono::system_clock::time_point tp) : tp_(tp) {}
    explicit FixedTimeSource(std::int64_t epoch_seconds)
        : tp_(std::chrono::system_clock::time_point(std::chrono::seconds(epoch_seconds))) {}

    // 覆寫注入的時間點。
    void set_now(std::chrono::system_clock::time_point tp) { tp_ = tp; }
    void set_epoch_seconds(std::int64_t epoch_seconds) {
        tp_ = std::chrono::system_clock::time_point(std::chrono::seconds(epoch_seconds));
    }

    std::chrono::system_clock::time_point now() const override { return tp_; }

private:
    std::chrono::system_clock::time_point tp_{};  // 預設 = epoch（1970-01-01T00:00:00Z）
};

// ---------------------------------------------------------------------------
// TimeDateProvider：把當前時間／日期掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標
// "time.now"，其三個可列舉實例（epoch / date / time）即當前時間的三個面向。取樣時刻
// 的時間點取自注入的 TimeSource——測試餵固定值即得決定性結果。消費者（掛件）只透過
// E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class TimeDateProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼。
    static constexpr const char* kMetricId = "time.now";
    // 提供者穩定識別碼（供診斷 / 去重 / 溯源）。
    static constexpr const char* kProviderId = "sysinfo.time";
    // 指標顯示名。
    static constexpr const char* kMetricName = "Current Time & Date";

    // 三個面向的實例識別碼。
    static constexpr const char* kInstanceEpoch = "epoch";
    static constexpr const char* kInstanceDate = "date";
    static constexpr const char* kInstanceTime = "time";

    // 以一個時間來源建構。source 為 null 時，提供者仍掛上指標，但三個實例值為
    // 「未知」（valid==false），保守而不崩、不謊報 0 為真實讀值。
    explicit TimeDateProvider(std::shared_ptr<TimeSource> source)
        : source_(std::move(source)) {}

    std::string provider_id() const override { return kProviderId; }

    // 對註冊表掛上 "time.now" 指標：取樣時間來源、建三個面向實例。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

private:
    std::shared_ptr<TimeSource> source_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_10_TIME_DATE_HPP
