// E2-22 登錄檔讀取 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：讀取「系統設定登錄／組態儲存」（Windows Registry 概念的**跨平台抽象**——
// 鍵路徑 → 值的階層式查詢）的 sysinfo 提供者，透過 **E2-01 的 MetricProvider 介面**
// 把查得的值掛成指標。這是「新增指標 = 新增 MetricProvider、掛件一行不動」機制的
// 一個具體提供者——它**消費 E2-01 契約、不自造指標模型**。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null/假後端**：**絕不**接真實 Registry / `defaults` / win32 /
//     RegOpenKey / `#ifdef`。改為**可注入 RegistrySource 抽象**（read(path) / enumerate(path)）
//     + null/假來源（記憶體樹）。真實登錄後端留待後端相位，本檔一律不含。
//   - 無 `#ifdef`、無系統呼叫、無平台分支——換平台一行不動（backend_guard 綠燈）。
//
// 錯誤語意（不靜默）：
//   - 查無鍵 → read() 回 std::nullopt；提供者暴露為「無讀值」（MetricValue::unknown）。
//   - 型別錯（以錯的型別讀取）→ 具型別存取器回 std::nullopt，明確回報而非硬轉。
#ifndef DS_MODULES_E2_22_REGISTRY_READ_HPP
#define DS_MODULES_E2_22_REGISTRY_READ_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"  // E2-01 契約（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// RegistryType：登錄值的型別（Windows REG_* 概念的跨平台最小集）
// ---------------------------------------------------------------------------
enum class RegistryType {
    String,   // REG_SZ 概念：文字值
    Integer,  // REG_DWORD / REG_QWORD 概念：整數值（統一為 int64）
    Binary,   // REG_BINARY 概念：位元組陣列
};

// ---------------------------------------------------------------------------
// RegistryValue：一個登錄值的平台中立、型別標記載體
// ---------------------------------------------------------------------------
// 帶型別標記；具型別存取器在型別不符時回 std::nullopt（**明確回報型別錯，不硬轉、不靜默**）。
class RegistryValue {
public:
    static RegistryValue makeString(std::string s) {
        RegistryValue v;
        v.type_ = RegistryType::String;
        v.str_ = std::move(s);
        return v;
    }
    static RegistryValue makeInteger(std::int64_t n) {
        RegistryValue v;
        v.type_ = RegistryType::Integer;
        v.int_ = n;
        return v;
    }
    static RegistryValue makeBinary(std::vector<std::uint8_t> b) {
        RegistryValue v;
        v.type_ = RegistryType::Binary;
        v.bytes_ = std::move(b);
        return v;
    }

    RegistryType type() const noexcept { return type_; }

    // 具型別存取：型別相符回值，否則回 nullopt（呼叫端據此得知型別錯，而非拿到亂值）。
    std::optional<std::string> as_string() const {
        if (type_ != RegistryType::String) return std::nullopt;
        return str_;
    }
    std::optional<std::int64_t> as_integer() const {
        if (type_ != RegistryType::Integer) return std::nullopt;
        return int_;
    }
    std::optional<std::vector<std::uint8_t>> as_binary() const {
        if (type_ != RegistryType::Binary) return std::nullopt;
        return bytes_;
    }

    bool operator==(const RegistryValue& o) const {
        if (type_ != o.type_) return false;
        switch (type_) {
            case RegistryType::String:  return str_ == o.str_;
            case RegistryType::Integer: return int_ == o.int_;
            case RegistryType::Binary:  return bytes_ == o.bytes_;
        }
        return false;
    }
    bool operator!=(const RegistryValue& o) const { return !(*this == o); }

private:
    RegistryType type_ = RegistryType::String;
    std::string str_;
    std::int64_t int_ = 0;
    std::vector<std::uint8_t> bytes_;
};

// ---------------------------------------------------------------------------
// RegistrySource：階層式登錄／組態儲存的抽象後端（平台中立契約）
// ---------------------------------------------------------------------------
// 鍵路徑以 '/' 分段（如 "HKLM/Software/App/Version"）。真實平台後端（相位 2+）實作它
// 以讀真實登錄；相位 1 只有 null/假來源。提供者只依賴此抽象，故換後端時提供者一行不動。
class RegistrySource {
public:
    // 路徑分段符。跨平台一致，與具體 OS 的分隔符（'\\' 等）解耦。
    static constexpr char kSeparator = '/';

    virtual ~RegistrySource() = default;

    // 讀取指定路徑的值。查無鍵 → std::nullopt（不靜默、不回假值）。
    virtual std::optional<RegistryValue> read(const std::string& path) const = 0;

    // 列舉指定鍵路徑下的**直屬子項名稱**（子鍵 / 值名），決定性順序。
    // 查無此鍵或其下無子項 → 空 vector。
    virtual std::vector<std::string> enumerate(const std::string& path) const = 0;

protected:
    RegistrySource() = default;
    RegistrySource(const RegistrySource&) = default;
    RegistrySource& operator=(const RegistrySource&) = default;
};

// ---------------------------------------------------------------------------
// NullRegistrySource：相位 1 的 null / 假後端（記憶體樹）
// ---------------------------------------------------------------------------
// **不接真實登錄**。以記憶體內的「路徑 → 值」映射模擬階層式儲存：
//   - 預設為空樹（Mac / null 期的誠實預設：讀任何鍵皆回 nullopt）。
//   - set_value / clear 注入假資料供測試與假感測器情境。
//   - enumerate 由已存值的路徑前綴推導直屬子項（決定性：字典序、去重）。
// 真實登錄掃描留待後端相位——本類永不含平台呼叫。
class NullRegistrySource : public RegistrySource {
public:
    NullRegistrySource() = default;

    // 於 path 設定 / 覆寫一個值。
    void set_value(const std::string& path, RegistryValue value) {
        values_[path] = std::move(value);
    }
    // 移除 path 的值；成功回 true，不存在回 false。
    bool remove(const std::string& path) { return values_.erase(path) != 0; }
    // 清空整棵樹（回到 null 期預設語意）。
    void clear() { values_.clear(); }

    std::size_t size() const noexcept { return values_.size(); }
    bool empty() const noexcept { return values_.empty(); }

    std::optional<RegistryValue> read(const std::string& path) const override;
    std::vector<std::string> enumerate(const std::string& path) const override;

private:
    // 有序映射：決定性列舉順序（字典序），亦利前綴掃描。
    std::map<std::string, RegistryValue> values_;
};

// ---------------------------------------------------------------------------
// registryValueToMetric：把一次讀取結果轉為 E2-01 的 MetricValue
// ---------------------------------------------------------------------------
// 對映規則（保守、不靜默）：
//   - std::nullopt（查無鍵）      → MetricValue::unknown()（valid==false，未知）
//   - Integer                    → of(數值, 十進位文字)
//   - String                     → of(0.0, 文字)（數值維度不適用，僅承載文字）
//   - Binary                     → of(0.0, "<binary:N bytes>")（承載位元組數描述）
ds::metrics::MetricValue registryValueToMetric(const std::optional<RegistryValue>& v);

// ---------------------------------------------------------------------------
// RegistryReaderProvider：把選定登錄鍵的值掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。以一組「要暴露的鍵路徑」建構；register_metrics()
// 內向註冊表掛上單一指標 "registry.values"，其**可列舉實例即各選定鍵**：
//   instance_id = 鍵路徑、label = 鍵路徑、value = 該鍵讀值（查無鍵則為未知）。
// 消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class RegistryReaderProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼。
    static constexpr const char* kMetricId = "registry.values";
    // 提供者穩定識別碼（供診斷 / 去重 / 溯源）。
    static constexpr const char* kProviderId = "sysinfo.registry";
    // 指標顯示名。
    static constexpr const char* kMetricName = "Registry Values";

    // 以一個登錄來源 + 要暴露的鍵路徑清單建構。source 為 null 或 keys 為空時，
    // 提供者仍會保守掛上一個「空 / 未知」指標，不崩。
    RegistryReaderProvider(std::shared_ptr<RegistrySource> source,
                           std::vector<std::string> keys)
        : source_(std::move(source)), keys_(std::move(keys)) {}

    std::string provider_id() const override { return kProviderId; }

    // 對註冊表掛上 "registry.values" 指標：每個選定鍵建一個實例、填入其讀值。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 選定鍵數（診斷用）。
    std::size_t key_count() const noexcept { return keys_.size(); }

private:
    std::shared_ptr<RegistrySource> source_;
    std::vector<std::string> keys_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_22_REGISTRY_READ_HPP
