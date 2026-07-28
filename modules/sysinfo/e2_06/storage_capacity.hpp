// E2-06 儲存容量 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「儲存容量」（每個磁碟 / 掛載點的 總容量 / 已用 / 可用 bytes 與使用率 %）透過
// **E2-01 的 MetricProvider 介面**掛成一組指標，並以 **E2-02 的採集頻率分級**決定採樣節奏。
// 這是「新增指標 = 新增 MetricProvider、掛件一行不動」機制的又一個具體提供者——它
// **消費 E2-01 / E2-02 契約、不自造指標模型或排程器**。本單元是最終「Disk / Storage
// Widget」的核心資料來源。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實檔案系統 / 磁碟 API**
//     （無 `statvfs` / `GetDiskFreeSpace` / `#ifdef` / win32 / cocoa / mach）。真實後端
//     （相位 2+）另實作抽象來源，提供者一行不動。
//   - 純資料快照，無差分：每次取樣直接讀各磁碟的 總 / 已用 / 可用 bytes；使用率由
//     `disk_usage_ratio(used, total)` 純算術得出（獨立可測）。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：一個磁碟 = **各指標裡的一個可列舉實例**
// （instance_id = 掛載點 / 裝置、label = 人類可讀名稱），支援多磁碟。因一個 MetricValue 只帶
// 一個數值維度，而本單元每磁碟要表達四個量，故掛上**四個平行指標**（各磁碟為其實例）：
//   - id="storage.usage"           name="Storage Usage"  unit="%"  range=bounded[0,100]
//   - id="storage.capacity.total"  name="Storage Total"  unit="B"  range=at_least(0)
//   - id="storage.capacity.used"   name="Storage Used"   unit="B"  range=at_least(0)
//   - id="storage.capacity.free"   name="Storage Free"   unit="B"  range=at_least(0)
// 四指標共用同一組每磁碟實例（同 instance_id / label），故掛件可對「同一顆磁碟」讀其
// 使用率與三個容量欄位。各實例保留時序歷史（history_capacity>0），配合 E2-02 週期採集
// 鋪成容量序列，供折線 / sparkline 類元件直接鋪繪。
#ifndef DS_MODULES_E2_06_STORAGE_CAPACITY_HPP
#define DS_MODULES_E2_06_STORAGE_CAPACITY_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 採集頻率分級（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// DiskCapacity：單一磁碟 / 掛載點的容量讀值（平台中立值）
// ---------------------------------------------------------------------------
// 跨平台一致的最小描述：穩定識別碼（掛載點 / 裝置）+ 顯示名 + 三個容量欄位（bytes）。
// 刻意不含任何平台專屬欄位（檔案系統型別 / 裝置節點旗標等），維持 module 層平台中立。
//   - total_bytes 磁碟 / 分割區的總容量。
//   - used_bytes  已用位元組。
//   - free_bytes  可用位元組。三者由來源各自給出——真實檔案系統常保留區塊，故
//     used + free 未必等於 total；本層不強制此不變式，使用率一律由 used / total 得出。
//   - valid       false = 「目前無讀值」（磁碟存在但無法查詢 / 尚未取樣）。消費者據此顯示
//                 為未知，而非把 0 誤當成真實讀值。
struct DiskCapacity {
    std::string id;             // 穩定實例識別碼（如 "/"、"/Volumes/Data"），跨平台一致
    std::string name;           // 人類可讀名稱（如 "Macintosh HD"）
    std::uint64_t total_bytes = 0;
    std::uint64_t used_bytes = 0;
    std::uint64_t free_bytes = 0;
    bool valid = false;         // false = 無讀值（未知），預設保守

    // 使用率比率 [0,1] = used / total（見 disk_usage_ratio 的邊界語意）。
    double usage_ratio() const noexcept;

    // 有效讀值工廠（valid==true）。
    static DiskCapacity of(std::string id, std::string name, std::uint64_t total,
                           std::uint64_t used, std::uint64_t free) {
        return DiskCapacity{std::move(id), std::move(name), total, used, free, true};
    }
    // 「存在但無讀值」工廠（valid==false）：磁碟仍列舉，但容量未知。
    static DiskCapacity unknown(std::string id, std::string name) {
        return DiskCapacity{std::move(id), std::move(name), 0, 0, 0, false};
    }

    bool operator==(const DiskCapacity& o) const {
        return valid == o.valid && id == o.id && name == o.name &&
               total_bytes == o.total_bytes && used_bytes == o.used_bytes &&
               free_bytes == o.free_bytes;
    }
    bool operator!=(const DiskCapacity& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// 使用率演算法（自由函式，獨立可測；平台中立純算術）
// ---------------------------------------------------------------------------
// 使用率比率 = used / total，夾到 [0,1]。
//   - total == 0（無容量 / 尚未讀到）→ 0（無從判斷，不謊報）。
//   - used > total（保留區塊等造成，理論不該發生）→ 夾到 1.0（滿載）。
double disk_usage_ratio(std::uint64_t used, std::uint64_t total) noexcept;

// ---------------------------------------------------------------------------
// StorageStatSource：儲存容量的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 enumerate() 回一份目前各磁碟的容量快照（決定性順序）。實作決定
// 其來源（注入假資料 / 真實後端）。真實平台後端（相位 2+）於此讀 statvfs / GetDiskFreeSpace；
// 相位 1 只有注入式 NullStorageStatSource。
class StorageStatSource {
public:
    virtual ~StorageStatSource() = default;

    // 列舉目前各磁碟的容量（順序即列舉順序，決定性）。無磁碟 / 無讀值回空 vector。
    virtual std::vector<DiskCapacity> enumerate() const = 0;

protected:
    StorageStatSource() = default;
    StorageStatSource(const StorageStatSource&) = default;
    StorageStatSource& operator=(const StorageStatSource&) = default;
};

// ---------------------------------------------------------------------------
// NullStorageStatSource：相位 1 的 null / 假來源
// ---------------------------------------------------------------------------
// **不接任何真實磁碟 API**。持一列注入的磁碟容量，enumerate() 直接回該列（決定性、無副作用）。
// 預設空列 = 無磁碟（Mac / null 期誠實預設）；可注入固定磁碟清單供測試與假感測器情境。
// 真實查詢留待後端相位——本類永不含平台呼叫。
class NullStorageStatSource : public StorageStatSource {
public:
    NullStorageStatSource() = default;
    explicit NullStorageStatSource(std::vector<DiskCapacity> disks)
        : disks_(std::move(disks)) {}

    // 注入 / 覆寫整份磁碟清單。
    void set_disks(std::vector<DiskCapacity> disks) { disks_ = std::move(disks); }
    // 追加一顆磁碟到清單尾。
    void add_disk(DiskCapacity d) { disks_.push_back(std::move(d)); }
    // 回到「無磁碟」預設（null 期誠實語意）。
    void clear() { disks_.clear(); }

    std::size_t size() const noexcept { return disks_.size(); }
    bool empty() const noexcept { return disks_.empty(); }
    const std::vector<DiskCapacity>& disks() const noexcept { return disks_; }

    std::vector<DiskCapacity> enumerate() const override { return disks_; }

private:
    std::vector<DiskCapacity> disks_;
};

// ---------------------------------------------------------------------------
// StorageCapacityProvider：把儲存容量掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上四個平行指標
// （usage / total / used / free），四者共用同一組每磁碟實例。因容量變動慢，本提供者建議以
// **E2-02 的低頻分級**採集（預設 Low）：呼叫端把各 metric_id 與 sampling_tier() 登記到
// SamplingScheduler，於排程器判定該採集時呼叫 sample() 重新讀來源並更新各實例（推入歷史）。
// 消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class StorageCapacityProvider : public ds::metrics::MetricProvider {
public:
    static constexpr const char* kProviderId = "sysinfo.storage";

    // 四個平行指標的識別碼 / 顯示名。
    static constexpr const char* kMetricUsage = "storage.usage";
    static constexpr const char* kMetricTotal = "storage.capacity.total";
    static constexpr const char* kMetricUsed = "storage.capacity.used";
    static constexpr const char* kMetricFree = "storage.capacity.free";
    static constexpr const char* kNameUsage = "Storage Usage";
    static constexpr const char* kNameTotal = "Storage Total";
    static constexpr const char* kNameUsed = "Storage Used";
    static constexpr const char* kNameFree = "Storage Free";

    static constexpr const char* kUnitPercent = "%";
    static constexpr const char* kUnitBytes = "B";

    // 使用率比率 [0,1] → 百分比 % 的縮放。
    static constexpr double kPercentScale = 100.0;

    // 各實例歷史環的預設容量（配合 E2-02 週期採集鋪成容量序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：容量變動慢，屬低頻（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Low;

    // 以一個儲存容量來源建構。source 為 null 時，提供者仍會掛上四個指標，且無磁碟實例
    // （保守而不崩、不謊報）。history 為各實例歷史環容量、tier 為建議採集分級。
    explicit StorageCapacityProvider(std::shared_ptr<StorageStatSource> source,
                                     std::size_t history = kDefaultHistory,
                                     ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 對註冊表掛上四個指標：取一份容量快照、建各磁碟實例、填初值，並保留指標參照供日後
    // sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新容量寫入各實例（推入歷史）。呼叫端在 E2-02 排程器判定本指標該採集時
    // 呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    // 若新取樣的磁碟數多於既有實例，會**動態新增**磁碟實例（既有參照不失效）；若較少，多出的
    // 磁碟實例設為未知（不污染歷史）。磁碟以列舉順序（索引）對齊。
    void sample();

    // 目前的磁碟實例數。register_metrics 前為 0。
    std::size_t disk_count() const noexcept { return disk_ids_.size(); }

private:
    // 一個指標槽：InMemoryMetric + 其各磁碟實例指標（與 disk_ids_ 同索引）。
    struct MetricSlot {
        std::shared_ptr<ds::metrics::InMemoryMetric> metric;
        std::vector<ds::metrics::InMemoryMetricInstance*> insts;
    };

    // 目前容量快照：source_ 為 null 時視為「無磁碟」。
    std::vector<DiskCapacity> current() const {
        return source_ ? source_->enumerate() : std::vector<DiskCapacity>{};
    }

    // 建一個指標槽（InMemoryMetric）。
    void make_slot(MetricSlot& slot, ds::metrics::MetricId id, std::string name,
                   std::string unit, ds::metrics::MetricRange range);

    // 把一份容量快照寫入四槽的各實例。to_history 為真時有效值推入歷史（採集路徑），
    // 為假時只設值不動歷史。必要時動態擴增磁碟實例。
    void apply(const std::vector<DiskCapacity>& disks, bool to_history);

    // 把一個數值寫入某實例（valid 決定是否為未知 / 是否推歷史）。
    void write_value(ds::metrics::InMemoryMetricInstance* inst, double number, bool valid,
                     bool to_history);

    std::shared_ptr<StorageStatSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有，供 sample() 更新（與 registry 共享同一物件，故更新對消費者
    // 可見）。四槽的實例指標指向各自 metric 內的實例（unique_ptr 持有，故新增更多磁碟實例時
    // 既有參照不失效）。disk_ids_ 記錄各索引的磁碟 instance_id（決定性順序、動態成長）。
    MetricSlot usage_;
    MetricSlot total_;
    MetricSlot used_;
    MetricSlot free_;
    std::vector<std::string> disk_ids_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_06_STORAGE_CAPACITY_HPP
