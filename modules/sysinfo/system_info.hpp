// E2-12 系統靜態資訊 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「系統靜態資訊」（OS 名稱／版本、主機名、CPU 型號、核心數等——隨選查詢、
// 非高頻）透過 **E2-01 的 MetricProvider 介面**掛成一個指標。這是「新增指標 = 新增
// MetricProvider、掛件一行不動」機制的一個具體提供者——它**消費 E2-01 契約、不自造
// 指標模型**。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null 後端**：不真的查系統。null 後端回空欄位集，或回注入的
//     假資料供測試。真實 OS 查詢（win32 / cocoa：uname / sysctl / GetVersionEx 等）
//     留待後端相位，本檔一律不含。
//   - 無 `#ifdef`、無系統呼叫、無平台分支——換平台一行不動（backend_guard 綠燈）。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id   = "system.static"
//   - name = "System Information"
//   - unit = ""（異質欄位集，無統一單位）
//   - range = unbounded（靜態資訊多為文字，無值域）
//   - **可列舉實例 = 各靜態欄位**：每個欄位一個 MetricInstance，
//     instance_id = 欄位鍵（如 "os.name"）、label = 人類可讀名（如 "OS Name"）、
//     value = 文字值（如 "macOS"）+ 可選數值維度（如核心數 8.0）。
//     於是「欄位集」由列舉實例取得、「欄位數」即 `Metric::instance_count()`——直接借用
//     E2-01 的可列舉實例要素，這正是本單元名「系統靜態資訊」的精神。
#ifndef DS_MODULES_E2_12_SYSTEM_INFO_HPP
#define DS_MODULES_E2_12_SYSTEM_INFO_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"  // E2-01 契約（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// SysInfoField：一項系統靜態資訊的平台中立描述
// ---------------------------------------------------------------------------
// 跨平台一致的最小描述：穩定鍵 + 顯示名 + 文字值 + 可選數值維度。
//   - key    穩定識別碼（如 "os.name"、"cpu.cores"），跨平台一致、命名同 E2-01 風格。
//   - label  人類可讀名稱（如 "OS Name"、"CPU Cores"）。
//   - text   文字值（如 "macOS"、"Apple M1"）；純數值欄位可為該數的文字表述。
//   - number 數值維度（如核心數 8.0）；純文字欄位維持 0.0（消費者讀 text）。
// 刻意不含任何平台專屬欄位，維持 module 層平台中立。
struct SysInfoField {
    std::string key;        // 穩定識別碼，跨平台一致
    std::string label;      // 人類可讀名稱
    std::string text;       // 文字值（顯示 / 列舉表述）
    double number = 0.0;    // 數值維度（純文字欄位為 0.0）

    bool operator==(const SysInfoField& o) const {
        return key == o.key && label == o.label && text == o.text && number == o.number;
    }
    bool operator!=(const SysInfoField& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// SysInfoSource：查詢系統靜態資訊的抽象後端（平台中立契約）
// ---------------------------------------------------------------------------
// 真實平台後端（相位 2+）實作它以隨選查詢系統；相位 1 只有 null 後端。提供者只依賴此
// 抽象介面，故換後端時提供者一行不動。
class SysInfoSource {
public:
    virtual ~SysInfoSource() = default;

    // 隨選查詢目前系統的靜態資訊欄位（順序即列舉順序，決定性）。
    virtual std::vector<SysInfoField> query() const = 0;

protected:
    SysInfoSource() = default;
    SysInfoSource(const SysInfoSource&) = default;
    SysInfoSource& operator=(const SysInfoSource&) = default;
};

// ---------------------------------------------------------------------------
// NullSysInfoSource：相位 1 的 null 後端
// ---------------------------------------------------------------------------
// **不查系統**。預設回空欄位集（Mac / null 期的誠實預設）；可注入假資料供測試與
// 假感測器情境。真實查詢留待後端相位——本類永不含平台呼叫。
class NullSysInfoSource : public SysInfoSource {
public:
    NullSysInfoSource() = default;
    explicit NullSysInfoSource(std::vector<SysInfoField> fields)
        : fields_(std::move(fields)) {}

    // 注入 / 覆寫整份假欄位集。
    void set_fields(std::vector<SysInfoField> fields) { fields_ = std::move(fields); }
    // 追加一個假欄位。
    void add_field(SysInfoField field) { fields_.push_back(std::move(field)); }
    // 清空為空欄位集（回到 null 期預設語意）。
    void clear() { fields_.clear(); }

    std::size_t size() const noexcept { return fields_.size(); }
    bool empty() const noexcept { return fields_.empty(); }

    // 回注入的欄位集副本（未注入則為空）。決定性順序 = 注入順序。
    std::vector<SysInfoField> query() const override { return fields_; }

private:
    std::vector<SysInfoField> fields_;
};

// ---------------------------------------------------------------------------
// SystemInfoProvider：把系統靜態資訊掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標
// "system.static"，其可列舉實例即各靜態欄位（欄位集由列舉取得、欄位數即實例數）。
// 消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class SystemInfoProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼。
    static constexpr const char* kMetricId = "system.static";
    // 提供者穩定識別碼（供診斷 / 去重 / 溯源）。
    static constexpr const char* kProviderId = "sysinfo.system";
    // 指標顯示名。
    static constexpr const char* kMetricName = "System Information";

    // 以一個系統資訊來源建構。source 為 null 時，提供者仍會掛上一個「空」指標
    // （instance_count()==0），保守而不崩。
    explicit SystemInfoProvider(std::shared_ptr<SysInfoSource> source)
        : source_(std::move(source)) {}

    std::string provider_id() const override { return kProviderId; }

    // 對註冊表掛上 "system.static" 指標：隨選查詢來源、每個欄位建一個實例。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

private:
    std::shared_ptr<SysInfoSource> source_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_12_SYSTEM_INFO_HPP
