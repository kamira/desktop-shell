// E2-04 記憶體使用量（實體/交換）— sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「記憶體使用量」——實體記憶體（總量／已用／可用 bytes、使用率 %）與交換空間
// swap（總量／已用）——透過 **E2-01 的 MetricProvider 介面**掛成一個指標，並以 **E2-02
// 的採集頻率分級**決定採樣節奏（記憶體用量會變動，宜週期採集）。這是「新增指標 = 新增
// MetricProvider、掛件一行不動」機制的又一個具體提供者——它**消費 E2-01 / E2-02 契約、
// 不自造指標模型或排程器**。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實記憶體 API**（無
//     `host_statistics` / `sysctl` / `/proc/meminfo` / mach / `#ifdef` / win32 / cocoa）。
//     真實後端（相位 2+）另實作抽象來源，提供者一行不動。
//   - 可注入的記憶體統計來源抽象 `MemoryStatSource` + null / 假來源 `NullMemoryStatSource`
//     （注入固定值或序列，模擬時間推進下的用量變化），故可完全單元測試。
//   - 無 `#ifdef`、無系統呼叫、無平台分支——換平台一行不動（backend_guard 綠燈）。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id    = "memory.stats"
//   - name  = "Memory"
//   - unit  = ""（異質欄位集：bytes 與 % 混雜，無統一單位；各欄位自帶語意）
//   - range = unbounded（欄位異質，無單一值域；使用率對總量的比例由消費者自算或讀使用率欄位）
//   - **可列舉實例 = 各記憶體欄位**：實體總量／已用／可用／使用率 % + swap 總量／已用，
//     每欄一個 MetricInstance（instance_id = 欄位鍵、label = 顯示名）。欄位集固定且決定性，
//     「無讀值」時各欄位以 valid==false（未知）誠實表達，而非塞假 0。變動欄位（已用／可用／
//     使用率 %／swap 已用）保留時序歷史（history_capacity>0），配合 E2-02 週期採集鋪成
//     用量序列；靜態欄位（實體總量／swap 總量）無歷史（capacity==0）。
#ifndef DS_MODULES_E2_04_MEMORY_USAGE_HPP
#define DS_MODULES_E2_04_MEMORY_USAGE_HPP

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
// MemoryStats：某一時刻的記憶體用量快照（平台中立值）
// ---------------------------------------------------------------------------
// 跨平台一致的最小描述，皆以 **bytes** 表達（不含任何取值方式）：
//   - physical_total      實體記憶體總量。
//   - physical_used       實體記憶體已用量。
//   - physical_available  實體記憶體可用量（可回收 / 空閒；某些 API 給 available，某些給 free，
//                         語意由來源決定——本層只誠實承載，不臆測）。
//   - swap_total          交換空間總量（無 swap 時為 0）。
//   - swap_used           交換空間已用量。
//   - valid               false = 「目前無讀值」（尚未取樣 / 感測失敗）。消費者據此顯示為
//                         未知，而非把 0 誤當真實讀值。
//
// used 與 available 由來源分別給出（不自動由 total-used 推導）：真實 API 因 buffer / cache
// 而未必 used + available == total，本層誠實承載來源所報，不臆造一致性。
struct MemoryStats {
    std::uint64_t physical_total = 0;      // 實體記憶體總量（bytes）
    std::uint64_t physical_used = 0;       // 實體記憶體已用（bytes）
    std::uint64_t physical_available = 0;  // 實體記憶體可用（bytes）
    std::uint64_t swap_total = 0;          // 交換空間總量（bytes）
    std::uint64_t swap_used = 0;           // 交換空間已用（bytes）
    bool valid = false;                    // false = 無讀值（未知），保守預設

    // 明確的「無讀值」（保守預設）。
    static MemoryStats unknown() { return MemoryStats{}; }

    // 便利工廠：以實體 total/used + swap total/used 建構，可用量 = total-used（夾到 >=0）。
    // valid==true。給只報 used 的來源用（available 由 total-used 推得）。
    static MemoryStats from_physical(std::uint64_t total, std::uint64_t used,
                                     std::uint64_t swap_total = 0,
                                     std::uint64_t swap_used = 0);

    // 實體記憶體使用率 %（used / total * 100），夾到 [0,100]。
    // total==0（無法判斷）→ 0.0（不謊報；欄位有效性另由 valid && total>0 決定）。
    double usage_percent() const noexcept;

    // 交換空間使用率 %（swap_used / swap_total * 100），夾到 [0,100]。
    // swap_total==0（無 swap）→ 0.0。
    double swap_usage_percent() const noexcept;

    bool operator==(const MemoryStats& o) const {
        return valid == o.valid && physical_total == o.physical_total &&
               physical_used == o.physical_used &&
               physical_available == o.physical_available &&
               swap_total == o.swap_total && swap_used == o.swap_used;
    }
    bool operator!=(const MemoryStats& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// MemoryStatSource：記憶體統計的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 read() 回一份 MemoryStats。實作決定其來源（注入 / 真實後端）。
// read() **非 const**：序列型來源每次讀取會推進內部游標，故取樣具副作用（與 E2-03
// CpuTickSource::read() 同慣例）。真實後端（相位 2+）於此讀 host_statistics /
// /proc/meminfo；相位 1 只有注入式 NullMemoryStatSource——本層永不含平台呼叫。
class MemoryStatSource {
public:
    virtual ~MemoryStatSource() = default;

    // 讀一份目前的記憶體用量快照。無讀值時回 MemoryStats::unknown()。
    virtual MemoryStats read() = 0;

protected:
    MemoryStatSource() = default;
    MemoryStatSource(const MemoryStatSource&) = default;
    MemoryStatSource& operator=(const MemoryStatSource&) = default;
};

// ---------------------------------------------------------------------------
// NullMemoryStatSource：相位 1 的 null / 假來源
// ---------------------------------------------------------------------------
// **不接任何真實記憶體 API**。以一列注入的快照表達：每次 read() 回下一份、列盡則持續回
// 最後一份（穩定，不走出界）、空列回 unknown（Mac / null 期的誠實預設）。**固定值**即
// 「一元素序列」（read 恆回該份）；**序列**模擬時間推進下的用量變化。真實查詢留待後端
// 相位——本類永不含平台呼叫。
class NullMemoryStatSource : public MemoryStatSource {
public:
    NullMemoryStatSource() = default;
    // 固定值：序列 = {fixed}（read 恆回該份）。
    explicit NullMemoryStatSource(MemoryStats fixed) : sequence_{std::move(fixed)} {}
    // 序列：模擬時間推進下的用量變化。
    explicit NullMemoryStatSource(std::vector<MemoryStats> sequence)
        : sequence_(std::move(sequence)) {}

    // 注入 / 覆寫為單一固定值（序列 = {fixed}，游標重置起點）。
    void set_stats(MemoryStats fixed) {
        sequence_ = std::vector<MemoryStats>{std::move(fixed)};
        cursor_ = 0;
    }
    // 注入 / 覆寫整條快照序列（游標重置起點）。
    void set_sequence(std::vector<MemoryStats> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }
    // 追加一份快照到序列尾。
    void push_sample(MemoryStats s) { sequence_.push_back(std::move(s)); }
    // 重置游標到序列起點。
    void reset() noexcept { cursor_ = 0; }
    // 清空為空序列（回到 null 期「無讀值」預設語意）。
    void clear() {
        sequence_.clear();
        cursor_ = 0;
    }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 回下一份快照；列盡持續回最後一份；空列回 MemoryStats::unknown()。
    MemoryStats read() override;

private:
    std::vector<MemoryStats> sequence_;
    std::size_t cursor_ = 0;
};

// ---------------------------------------------------------------------------
// MemoryProvider：把記憶體用量掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標
// "memory.stats"，其可列舉實例即各記憶體欄位（實體總量／已用／可用／使用率 % + swap
// 總量／已用）。因用量會隨時間變動，本提供者建議以 **E2-02 的週期分級**採集（記憶體用量
// 屬常規頻率，預設 Normal）：呼叫端把 metric_id 與 sampling_tier() 登記到
// SamplingScheduler，於排程器判定該採集時呼叫 sample() 重新讀來源並更新各實例（變動欄位
// 推入歷史）。消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及
// 本型別。
class MemoryProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼 / 顯示名 / 提供者識別碼。
    static constexpr const char* kMetricId = "memory.stats";
    static constexpr const char* kProviderId = "sysinfo.memory";
    static constexpr const char* kMetricName = "Memory";

    // 各欄位實例的穩定鍵（命名同 E2-01 風格；供消費者以 find_instance 尋值）。
    static constexpr const char* kFieldPhysicalTotal = "memory.physical.total";
    static constexpr const char* kFieldPhysicalUsed = "memory.physical.used";
    static constexpr const char* kFieldPhysicalAvailable = "memory.physical.available";
    static constexpr const char* kFieldUsagePercent = "memory.usage.percent";
    static constexpr const char* kFieldSwapTotal = "memory.swap.total";
    static constexpr const char* kFieldSwapUsed = "memory.swap.used";

    // 各欄位的人類可讀標籤。
    static constexpr const char* kPhysicalTotalLabel = "Physical Total";
    static constexpr const char* kPhysicalUsedLabel = "Physical Used";
    static constexpr const char* kPhysicalAvailableLabel = "Physical Available";
    static constexpr const char* kUsagePercentLabel = "Memory Usage";
    static constexpr const char* kSwapTotalLabel = "Swap Total";
    static constexpr const char* kSwapUsedLabel = "Swap Used";

    // 各欄位自帶單位（metric 層 unit="" 因欄位異質；各欄位語意在此）。
    static constexpr const char* kUnitBytes = "bytes";
    static constexpr const char* kUnitPercent = "%";

    // 變動欄位歷史環的預設容量（配合 E2-02 週期採集鋪成用量序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：記憶體用量變動屬常規頻率（非高頻動畫、亦非隨選靜態）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Normal;

    // 以一個記憶體統計來源建構。source 為 null 時，提供者仍會掛上指標，且各欄位以
    // 「無讀值」（valid==false）呈現（保守而不崩、不謊報 0）。history 為變動欄位歷史環容量、
    // tier 為建議採集分級。
    explicit MemoryProvider(std::shared_ptr<MemoryStatSource> source,
                            std::size_t history = kDefaultHistory,
                            ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 對註冊表掛上 "memory.stats" 指標：建立固定欄位集、以目前一份用量填初值，並保留
    // 指標參照供日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新用量寫入各實例（變動欄位推入歷史）。呼叫端在 E2-02 排程器判定
    // 本指標該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op（保守不崩）。
    void sample();

private:
    // 依一份用量把值寫入各實例。to_history 為真時變動欄位推入歷史（採集路徑），
    // 為假時只設值不動歷史。
    void apply(const MemoryStats& stats, bool to_history);

    // 目前用量快照：source_ 為 null 時視為「無讀值」。
    MemoryStats current() {
        return source_ ? source_->read() : MemoryStats::unknown();
    }

    // 把一個 bytes 數值寫入某實例（valid 決定是否為未知 / 是否推歷史）。
    void write_bytes(ds::metrics::InMemoryMetricInstance* inst, double bytes, bool valid,
                     bool to_history);
    // 把一個百分比寫入某實例（附 % 文字表述；valid 決定未知 / 推歷史）。
    void write_percent(ds::metrics::InMemoryMetricInstance* inst, double percent, bool valid,
                       bool to_history);

    std::shared_ptr<MemoryStatSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有，供 sample() 更新（與 registry 共享同一物件，故更新對
    // 消費者可見）。非擁有指標指向 metric_ 內的實例（其壽命由 metric_ 保證，unique_ptr
    // 持有，故既有參照不失效）。
    std::shared_ptr<ds::metrics::InMemoryMetric> metric_;
    ds::metrics::InMemoryMetricInstance* inst_phys_total_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_phys_used_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_phys_avail_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_usage_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_swap_total_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_swap_used_ = nullptr;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_04_MEMORY_USAGE_HPP
