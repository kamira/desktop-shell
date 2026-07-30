// E2-25 剪貼簿監看 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「剪貼簿內容／變更」透過 **E2-01 的 MetricProvider 介面**掛成一個指標，
// 並以 **E2-02 的採集頻率分級**把它歸為低頻採集（剪貼簿變動慢、非前景動畫，屬 Low 級）。
// 這是「新增指標 = 新增 `MetricProvider`、掛件一行不動」機制的一個具體提供者——它
// **消費 E2-01 / E2-02 契約、不自造指標模型、不自造排程**。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null 後端**：不真的讀系統剪貼簿。null 後端回空內容，或回
//     注入的假內容供測試。真實 OS 剪貼簿存取（win32 / cocoa）留待後端相位，本檔一律不含。
//   - 無 `#ifdef`、無系統呼叫、無平台分支——換平台一行不動（backend_guard 綠燈）。
//   - **變更偵測為純邏輯**：比對前後兩份快照，可完全單元測試（注入快照序列，驗變更旗標
//     與累計變更次數），不依賴真實時鐘或真實剪貼簿。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id    = "clipboard.content"
//   - name  = "Clipboard"
//   - unit  = ""（數值維度為「累計變更次數」，無單位）
//   - range = at_least(0)（下界 0、上無界；變更次數單調不減）
//   - 單一實例（剪貼簿為單值來源）：
//       value.number = 累計變更次數（可繪 sparkline / 正規化）
//       value.text   = 目前剪貼簿文字內容（""＝空剪貼簿）
//       value.valid  = 是否已至少採集一次（採集前為 unknown，不把 0 誤當真實讀值）
//       history      = 累計變更次數的環狀歷史（供折線呈現變更活動）
#ifndef DS_MODULES_E2_25_CLIPBOARD_MONITOR_HPP
#define DS_MODULES_E2_25_CLIPBOARD_MONITOR_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 契約（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// ClipboardSnapshot：剪貼簿某一刻的平台中立快照
// ---------------------------------------------------------------------------
// 跨平台一致的最小描述：`present`（剪貼簿是否有文字項）＋ `text`（其內容）。
// 刻意不含任何平台專屬欄位（NSPasteboard changeCount / Win32 clipboard sequence 等），
// 維持 module 層平台中立。`present==false` 表示空剪貼簿（此時 text 應為空）。
struct ClipboardSnapshot {
    bool present = false;   // 剪貼簿是否有文字內容
    std::string text;       // 內容文字（present==false 時為空）

    // 空剪貼簿（無內容）。
    static ClipboardSnapshot empty() { return ClipboardSnapshot{false, {}}; }
    // 帶文字內容的剪貼簿。
    static ClipboardSnapshot of(std::string t) { return ClipboardSnapshot{true, std::move(t)}; }

    bool operator==(const ClipboardSnapshot& o) const {
        return present == o.present && text == o.text;
    }
    bool operator!=(const ClipboardSnapshot& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// ClipboardSource：讀取剪貼簿目前內容的抽象後端（平台中立契約）
// ---------------------------------------------------------------------------
// 真實平台後端（相位 2+）實作它以讀取系統剪貼簿；相位 1 只有 null 後端。提供者只依賴
// 此抽象介面，故換後端時提供者一行不動。
class ClipboardSource {
public:
    virtual ~ClipboardSource() = default;

    // 讀取目前剪貼簿快照。決定性（同一狀態多次讀取結果一致）。
    virtual ClipboardSnapshot read() const = 0;

protected:
    ClipboardSource() = default;
    ClipboardSource(const ClipboardSource&) = default;
    ClipboardSource& operator=(const ClipboardSource&) = default;
};

// ---------------------------------------------------------------------------
// NullClipboardSource：相位 1 的 null 後端
// ---------------------------------------------------------------------------
// **不讀系統剪貼簿**。預設回空內容（Mac / null 期的誠實預設）；可注入假內容供測試與
// 假感測器情境。真實讀取留待後端相位——本類永不含平台呼叫。
class NullClipboardSource : public ClipboardSource {
public:
    NullClipboardSource() = default;
    explicit NullClipboardSource(ClipboardSnapshot snap) : snap_(std::move(snap)) {}

    // 注入 / 覆寫整份假快照。
    void set_snapshot(ClipboardSnapshot snap) { snap_ = std::move(snap); }
    // 便利：以文字內容注入（等同 set_snapshot(ClipboardSnapshot::of(text))）。
    void set_text(std::string text) { snap_ = ClipboardSnapshot::of(std::move(text)); }
    // 清空為空剪貼簿（回到 null 期預設語意）。
    void clear() { snap_ = ClipboardSnapshot::empty(); }

    bool empty() const noexcept { return !snap_.present; }

    // 回目前注入的快照副本（未注入則為空剪貼簿）。
    ClipboardSnapshot read() const override { return snap_; }

private:
    ClipboardSnapshot snap_ = ClipboardSnapshot::empty();
};

// ---------------------------------------------------------------------------
// ClipboardMonitor：純邏輯的變更偵測器
// ---------------------------------------------------------------------------
// 記住上一份快照，比對新快照以判定「是否變更」並累計變更次數。**純邏輯、可完全單元測試**
// （注入快照序列，驗變更旗標與累計次數），不碰真實剪貼簿與時鐘。
//
// 起始語意：初始「前一份」為空剪貼簿（present==false、text==""）。故第一次觀察到非空內容
// 即算一次變更；若第一次觀察即為空，則不算變更（與初始態相同）。
class ClipboardMonitor {
public:
    ClipboardMonitor() = default;

    // 觀察一份新快照。與上一份不同則累計次數 +1、更新目前快照、回 true；相同回 false。
    bool observe(const ClipboardSnapshot& snap) {
        ++observed_;
        if (snap != current_) {
            current_ = snap;
            ++changes_;
            return true;
        }
        return false;
    }

    // 目前（最近一次觀察到的）快照。未曾觀察時為初始空剪貼簿。
    const ClipboardSnapshot& current() const noexcept { return current_; }

    // 累計變更次數（單調不減）。
    std::size_t change_count() const noexcept { return changes_; }

    // 已觀察次數（呼叫 observe 的總數，含未變更者）。
    std::size_t observed_count() const noexcept { return observed_; }

    // 是否至少觀察過一次。
    bool has_observed() const noexcept { return observed_ != 0; }

    // 重置為初始態（空快照、次數歸零）。
    void reset() {
        current_ = ClipboardSnapshot::empty();
        changes_ = 0;
        observed_ = 0;
    }

private:
    ClipboardSnapshot current_ = ClipboardSnapshot::empty();
    std::size_t changes_ = 0;    // 累計變更次數
    std::size_t observed_ = 0;   // 累計觀察次數
};

// ---------------------------------------------------------------------------
// ClipboardMonitorProvider：把剪貼簿內容／變更掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標
// "clipboard.content"（單一實例）。之後每次採集迴圈到期，呼叫 sample() 讀來源、經
// ClipboardMonitor 偵測變更、更新該實例的 value 與歷史。消費者（掛件）只透過 E2-01 的
// MetricRegistry / Metric 介面走訪，完全不觸及本型別。
//
// 與 E2-02 的關係：剪貼簿屬「變動慢、非前景」的低頻指標，建議採集分級為
// SamplingTier::Low。register_demand() 便利地把本指標以 Low 級登記進 E2-02 排程器
// （除頻合併沿用 E2-02，不自造排程）。
class ClipboardMonitorProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼。
    static constexpr const char* kMetricId = "clipboard.content";
    // 提供者穩定識別碼（供診斷 / 去重 / 溯源）。
    static constexpr const char* kProviderId = "sysinfo.clipboard";
    // 指標顯示名。
    static constexpr const char* kMetricName = "Clipboard";
    // 建議採集分級（E2-02）：剪貼簿變動慢，歸低頻。
    static constexpr ds::metrics::SamplingTier kSuggestedTier =
        ds::metrics::SamplingTier::Low;
    // 變更次數歷史環的預設容量。
    static constexpr std::size_t kDefaultHistoryCapacity = 32;

    // 以一個剪貼簿來源建構。source 為 null 時，提供者仍會掛上指標並在 sample() 時保守
    // 視為空剪貼簿（不崩）。history_capacity 為變更次數歷史環容量。
    explicit ClipboardMonitorProvider(std::shared_ptr<ClipboardSource> source,
                                      std::size_t history_capacity = kDefaultHistoryCapacity)
        : source_(std::move(source)), history_capacity_(history_capacity) {}

    std::string provider_id() const override { return kProviderId; }

    // 對註冊表掛上 "clipboard.content" 指標（單一實例、初始 value 為 unknown，
    // 表「尚未採集」）。掛上後由 sample() 驅動更新。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 採集一次：讀來源 → 變更偵測 → 更新指標實例 value 與歷史。回傳本次是否偵測到變更。
    // 未先 register_metrics（無實例）時為 no-op，回 false。source 為 null 視為空剪貼簿。
    bool sample();

    // 便利：以 E2-02 排程器把本指標以建議分級（Low）登記為一筆需求，回傳票根。
    // 除頻合併與到期判定沿用 E2-02，本提供者不自造排程。
    ds::metrics::DemandId register_demand(ds::metrics::SamplingScheduler& scheduler) const {
        return scheduler.add_demand(kMetricId, kSuggestedTier);
    }

    // 目前累計變更次數（變更偵測器狀態）。
    std::size_t change_count() const noexcept { return monitor_.change_count(); }

    // 目前（最近一次採集到的）剪貼簿快照。
    const ClipboardSnapshot& current() const noexcept { return monitor_.current(); }

    // 已掛上的指標（未 register 前為 nullptr）。
    std::shared_ptr<ds::metrics::Metric> metric() const { return metric_; }

private:
    std::shared_ptr<ClipboardSource> source_;
    std::size_t history_capacity_;
    ClipboardMonitor monitor_;
    std::shared_ptr<ds::metrics::InMemoryMetric> metric_;
    ds::metrics::InMemoryMetricInstance* instance_ = nullptr;  // 由 metric_ 持有，僅弱參照
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_25_CLIPBOARD_MONITOR_HPP
