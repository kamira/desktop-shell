// E2-16 已安裝應用列舉 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「已安裝應用清單／數量」透過 **E2-01 的 MetricProvider 介面**掛成一個指標。
// 這是「新增指標 = 新增 MetricProvider、掛件一行不動」機制的一個具體提供者——它
// **消費 E2-01 契約、不自造指標模型**。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null 後端**：不真的掃描系統。null 後端回空清單，或回注入的
//     假資料供測試。真實 OS 掃描（win32 / cocoa）留待後端相位，本檔一律不含。
//   - 無 `#ifdef`、無系統呼叫、無平台分支——換平台一行不動（backend_guard 綠燈）。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id   = "apps.installed"
//   - name = "Installed Applications"
//   - unit = ""（純計數，無單位）
//   - range = at_least(0)（下界 0、上無界）
//   - **可列舉實例 = 已安裝的各應用**：每個應用一個 MetricInstance，
//     instance_id = 應用穩定識別碼、label = 人類可讀名稱、value = 存在(1.0) + 版本文字。
//     於是「清單」由列舉實例取得、「數量」即 `Metric::instance_count()`——直接借用
//     E2-01 的可列舉實例要素，這正是本單元名「已安裝應用列舉」的精神。
#ifndef DS_MODULES_E2_16_INSTALLED_APPS_HPP
#define DS_MODULES_E2_16_INSTALLED_APPS_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"  // E2-01 契約（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// InstalledApp：一個已安裝應用的平台中立描述
// ---------------------------------------------------------------------------
// 跨平台一致的最小描述：穩定識別碼 + 顯示名 + 可選版本。刻意不含任何平台專屬欄位
// （bundle 路徑 / 登錄機碼等），維持 module 層平台中立。
struct InstalledApp {
    std::string id;       // 穩定識別碼（如 bundle id / 套件名），跨平台一致
    std::string name;     // 人類可讀名稱（如 "Safari"）
    std::string version;  // 版本字串；"" = 未知

    bool operator==(const InstalledApp& o) const {
        return id == o.id && name == o.name && version == o.version;
    }
    bool operator!=(const InstalledApp& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// InstalledAppSource：列舉已安裝應用的抽象後端（平台中立契約）
// ---------------------------------------------------------------------------
// 真實平台後端（相位 2+）實作它以掃描系統；相位 1 只有 null 後端。提供者只依賴此
// 抽象介面，故換後端時提供者一行不動。
class InstalledAppSource {
public:
    virtual ~InstalledAppSource() = default;

    // 列舉目前已安裝的應用（順序即列舉順序，決定性）。
    virtual std::vector<InstalledApp> enumerate() const = 0;

protected:
    InstalledAppSource() = default;
    InstalledAppSource(const InstalledAppSource&) = default;
    InstalledAppSource& operator=(const InstalledAppSource&) = default;
};

// ---------------------------------------------------------------------------
// NullInstalledAppSource：相位 1 的 null 後端
// ---------------------------------------------------------------------------
// **不掃描系統**。預設回空清單（Mac / null 期的誠實預設）；可注入假資料供測試與
// 假感測器情境。真實掃描留待後端相位——本類永不含平台呼叫。
class NullInstalledAppSource : public InstalledAppSource {
public:
    NullInstalledAppSource() = default;
    explicit NullInstalledAppSource(std::vector<InstalledApp> apps)
        : apps_(std::move(apps)) {}

    // 注入 / 覆寫整份假清單。
    void set_apps(std::vector<InstalledApp> apps) { apps_ = std::move(apps); }
    // 追加一個假應用。
    void add_app(InstalledApp app) { apps_.push_back(std::move(app)); }
    // 清空為空清單（回到 null 期預設語意）。
    void clear() { apps_.clear(); }

    std::size_t size() const noexcept { return apps_.size(); }
    bool empty() const noexcept { return apps_.empty(); }

    // 回注入的清單副本（未注入則為空）。決定性順序 = 注入順序。
    std::vector<InstalledApp> enumerate() const override { return apps_; }

private:
    std::vector<InstalledApp> apps_;
};

// ---------------------------------------------------------------------------
// InstalledAppsProvider：把已安裝應用清單／數量掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標
// "apps.installed"，其可列舉實例即各已安裝應用（清單由列舉取得、數量即實例數）。
// 消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class InstalledAppsProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼。
    static constexpr const char* kMetricId = "apps.installed";
    // 提供者穩定識別碼（供診斷 / 去重 / 溯源）。
    static constexpr const char* kProviderId = "sysinfo.apps";
    // 指標顯示名。
    static constexpr const char* kMetricName = "Installed Applications";

    // 以一個應用來源建構。source 為 null 時，提供者仍會掛上一個「空」指標
    // （instance_count()==0），保守而不崩。
    explicit InstalledAppsProvider(std::shared_ptr<InstalledAppSource> source)
        : source_(std::move(source)) {}

    std::string provider_id() const override { return kProviderId; }

    // 對註冊表掛上 "apps.installed" 指標：列舉來源、每個應用建一個實例。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

private:
    std::shared_ptr<InstalledAppSource> source_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_16_INSTALLED_APPS_HPP
