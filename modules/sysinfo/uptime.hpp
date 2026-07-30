// E2-11 系統運行時間 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「系統運行時間 uptime」（開機至今的秒數，可衍生天/時/分格式化文字；並可選帶
// 開機時間戳）透過 **E2-01 的 MetricProvider 介面**掛成一個指標，並以 **E2-02 的採集頻率
// 分級**決定採樣節奏。這是「新增指標 = 新增 MetricProvider、掛件一行不動」機制的又一個
// 具體提供者——它**消費 E2-01 / E2-02 契約、不自造指標模型或排程器**。uptime 變動緩慢
// 且單調遞增（每秒 +1 秒），故建議屬低頻週期採集。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實時間 / uptime API**（無
//     `sysctl kern.boottime` / `/proc/uptime` / `GetTickCount` / `mach_absolute_time` /
//     `#ifdef` / win32 / cocoa）。真實後端（相位 2+）另實作抽象來源，提供者一行不動。
//   - 運行時間由**可注入的 UptimeSource 抽象**供給：相位 1 只有 `NullUptimeSource`，
//     可注入固定值或**遞增序列**（模擬時間推進下的 uptime 累積），供測試與假感測器情境。
//   - 無讀值時以 `valid==false`（未知）誠實表達，而非把 0 秒誤當真實讀值。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id    = "system.uptime"
//   - name  = "System Uptime"
//   - unit  = "s"（秒）
//   - range = at_least(0)（下界 0、上無界——uptime 只增不減，無自然上限）
//   - **可列舉實例**：
//       * "uptime" — label "System Uptime"，value = 秒數（number）+ 格式化文字
//         （"Nd HH:MM:SS"，text），保留歷史環（配合 E2-02 週期採集鋪成序列）。
//       * "boot.time" — label "Boot Time"，**可選**：僅當來源提供開機時間戳時建立，
//         value = 開機時間戳（number；靜態，無歷史）。
#ifndef DS_MODULES_E2_11_UPTIME_HPP
#define DS_MODULES_E2_11_UPTIME_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 採集頻率分級（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// UptimeReading：某一時刻的系統運行時間快照（平台中立值）
// ---------------------------------------------------------------------------
// 提供者面對的統一 uptime 形狀（無論來自真實後端或注入式假來源）：
//   - seconds         開機至今的秒數（>= 0，單調遞增）。
//   - boot_unix_time  **可選**開機時間戳（Unix 秒）；無則為 nullopt（多數情境只有 uptime）。
//   - valid           false = 「目前無讀值」（尚未取樣 / 感測失敗）。消費者據此顯示為未知，
//                     而非把 0 秒誤當真實讀值。
struct UptimeReading {
    double seconds = 0.0;                       // 開機至今秒數（>= 0）
    std::optional<double> boot_unix_time;       // 可選開機時間戳（Unix 秒）
    bool valid = false;                         // false = 無讀值（未知），保守預設

    // 有效 uptime（僅秒數，無開機時間戳）。
    static UptimeReading of(double secs) { return UptimeReading{secs, std::nullopt, true}; }
    // 有效 uptime + 開機時間戳。
    static UptimeReading of(double secs, double boot_ts) {
        return UptimeReading{secs, boot_ts, true};
    }
    // 明確的「無讀值」（保守預設）。
    static UptimeReading unknown() { return UptimeReading{}; }

    bool operator==(const UptimeReading& o) const {
        return valid == o.valid && seconds == o.seconds && boot_unix_time == o.boot_unix_time;
    }
    bool operator!=(const UptimeReading& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// format_uptime：把秒數格式化為 "Nd HH:MM:SS"（自由函式，獨立可測；純算術）
// ---------------------------------------------------------------------------
// 由秒數衍生「天/時/分/秒」的人類可讀表述（供顯示 / 列舉表述），如：
//   0      → "0d 00:00:00"
//   42     → "0d 00:00:42"
//   3661   → "0d 01:01:01"
//   90061  → "1d 01:01:01"
// 負值（理論不該發生，uptime 只增）視為 0（保守，不謊報）；小數秒截斷取整。
std::string format_uptime(double seconds);

// ---------------------------------------------------------------------------
// UptimeSource：查詢系統運行時間的抽象後端（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 read() 回一份 UptimeReading。真實平台後端（相位 2+）於此讀
// `sysctl kern.boottime` / `/proc/uptime` / `GetTickCount` 等；相位 1 只有注入式
// `NullUptimeSource`。read() **非 const**：序列型來源每次取樣會推進內部游標，故取樣具副作用。
class UptimeSource {
public:
    virtual ~UptimeSource() = default;

    // 取一份目前的運行時間快照。無讀值時回 UptimeReading::unknown()。
    virtual UptimeReading read() = 0;

protected:
    UptimeSource() = default;
    UptimeSource(const UptimeSource&) = default;
    UptimeSource& operator=(const UptimeSource&) = default;
};

// ---------------------------------------------------------------------------
// NullUptimeSource：相位 1 的 null / 假來源
// ---------------------------------------------------------------------------
// **不接任何真實時間 / uptime API**。預設回「無讀值」（Mac / null 期的誠實預設）；可注入：
//   - **固定值**（set_reading）：每次 read() 回同一份（模擬穩定讀值）。
//   - **遞增序列**（set_sequence / push_reading）：每次 read() 回序列下一份，列盡則持續回
//     最後一份（穩定，不走出界）——模擬時間推進下 uptime 逐步累積。序列非空時優先於固定值。
// 真實查詢留待後端相位——本類永不含平台呼叫。
class NullUptimeSource : public UptimeSource {
public:
    NullUptimeSource() = default;
    // 以固定讀值建構。
    explicit NullUptimeSource(UptimeReading fixed) : fixed_(std::move(fixed)) {}
    // 以遞增序列建構。
    explicit NullUptimeSource(std::vector<UptimeReading> sequence)
        : sequence_(std::move(sequence)) {}

    // 設定固定讀值（不影響已注入的序列；序列非空時 read() 仍優先走序列）。
    void set_reading(UptimeReading r) { fixed_ = std::move(r); }
    // 便利：以純秒數設定固定讀值。
    void set_seconds(double secs) { fixed_ = UptimeReading::of(secs); }

    // 注入 / 覆寫整條遞增序列（重置游標到起點）。
    void set_sequence(std::vector<UptimeReading> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }
    // 追加一份讀值到序列尾。
    void push_reading(UptimeReading r) { sequence_.push_back(std::move(r)); }
    // 重置序列游標到起點。
    void reset() noexcept { cursor_ = 0; }
    // 清空為「無讀值」預設（回到 null 期誠實語意：固定值 unknown、序列清空）。
    void clear() {
        fixed_ = UptimeReading::unknown();
        sequence_.clear();
        cursor_ = 0;
    }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 序列非空 → 回下一份（列盡持續回最後一份）；否則回固定值。決定性。
    UptimeReading read() override;

private:
    UptimeReading fixed_ = UptimeReading::unknown();  // 序列為空時的回值
    std::vector<UptimeReading> sequence_;             // 非空時優先，逐份推進
    std::size_t cursor_ = 0;                          // 序列游標
};

// ---------------------------------------------------------------------------
// UptimeProvider：把系統運行時間掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標
// "system.uptime"，其可列舉實例 = 一個 "uptime"（秒數 + 格式化文字，保留歷史）+ 可選的
// "boot.time"（開機時間戳，靜態）。因 uptime 隨時間變動（單調遞增），本提供者建議以 **E2-02
// 的週期分級**採集（uptime 變動緩慢，預設 Low）：呼叫端把 metric_id 與 sampling_tier() 登記
// 到 SamplingScheduler，於排程器判定該採集時呼叫 sample() 重新讀來源並更新實例（uptime 推入
// 歷史）。消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class UptimeProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼 / 顯示名 / 單位。
    static constexpr const char* kMetricId = "system.uptime";
    static constexpr const char* kProviderId = "sysinfo.uptime";
    static constexpr const char* kMetricName = "System Uptime";
    static constexpr const char* kUnit = "s";

    // uptime 實例的穩定識別碼與顯示名。
    static constexpr const char* kInstanceUptime = "uptime";
    static constexpr const char* kUptimeLabel = "System Uptime";
    // 可選開機時間戳實例的穩定識別碼與顯示名。
    static constexpr const char* kInstanceBoot = "boot.time";
    static constexpr const char* kBootLabel = "Boot Time";

    // uptime 實例歷史環的預設容量（配合 E2-02 週期採集鋪成序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：uptime 變動緩慢，屬低頻（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Low;

    // 以一個 uptime 來源建構。source 為 null 時，提供者仍會掛上指標，且 uptime 實例以
    // 「未知」（valid==false）呈現、無開機時間戳實例（保守而不崩、不謊報 0）。history 為
    // uptime 實例歷史環容量、tier 為建議採集分級。
    explicit UptimeProvider(std::shared_ptr<UptimeSource> source,
                            std::size_t history = kDefaultHistory,
                            ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 對註冊表掛上 "system.uptime" 指標：取一份運行時間、建 uptime 實例（並依讀值決定是否
    // 建開機時間戳實例）、填初值，並保留指標參照供日後 sample() 更新。重複 id 由註冊表保守
    // 拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新運行時間寫入實例（uptime 推入歷史）。呼叫端在 E2-02 排程器判定
    // 本指標該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op（保守不崩）。
    // 若讀值首次帶開機時間戳而先前尚無該實例，會**動態新增** "boot.time" 實例（既有參照
    // 不失效）。
    void sample();

    // 是否已建立開機時間戳實例（來源曾提供過有效開機時間戳）。register_metrics 前為 false。
    bool has_boot_time() const noexcept { return inst_boot_ != nullptr; }

private:
    // 把一份讀值寫入各實例。to_history 為真時 uptime 推入歷史（採集路徑），為假時只設值。
    void apply(const UptimeReading& r, bool to_history);

    // 目前運行時間快照：source_ 為 null 時視為「無讀值」。
    UptimeReading current() {
        return source_ ? source_->read() : UptimeReading::unknown();
    }

    std::shared_ptr<UptimeSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有，供 sample() 更新（與 registry 共享同一物件，故更新對消費者
    // 可見）。非擁有指標指向 metric_ 內的實例（其壽命由 metric_ 保證，unique_ptr 持有，故
    // 動態新增開機時間戳實例時既有參照不失效）。
    std::shared_ptr<ds::metrics::InMemoryMetric> metric_;
    ds::metrics::InMemoryMetricInstance* inst_uptime_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_boot_ = nullptr;  // 可選（來源給時才建）
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_11_UPTIME_HPP
