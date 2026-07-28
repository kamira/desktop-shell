// E2-07 儲存 IO 吞吐與佇列 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「磁碟 IO 指標」透過 **E2-01 的 MetricProvider 介面** 掛成一組指標——讀 / 寫
// **吞吐率**（bytes/sec）、讀 / 寫 **IOPS**（每秒操作數）、**佇列深度**（queue depth）——
// 並以 **E2-02 的採集頻率分級** 決定採樣節奏，支援**多磁碟**（每顆磁碟一個可列舉實例）。
// 這是「新增指標 = 新增 MetricProvider、掛件一行不動」機制的又一個具體提供者——它
// **消費 E2-01 / E2-02 契約、不自造指標模型或排程器**。本單元是最終「磁碟活動 Widget」的
// 核心資料來源，故介面刻意乾淨、易組合（差分邏輯與提供者分離、來源可注入、來源可鏈接）。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實 IO API**（無 IOKit /
//     `/proc/diskstats` / mach / `#ifdef` / win32 / cocoa）。真實後端（相位 2+）另實作抽象
//     來源，提供者一行不動。
//   - **吞吐率 / IOPS 由累積計數差分算出**：作業系統原生的磁碟統計多為單調遞增的累積計數
//     （累積 read/write bytes、累積 read/write 操作數）。速率須由**兩次取樣差分**得出——
//     注入兩份累積計數 + 各自時間戳，速率 = 計數Δ / 時間差（bytes/sec、ops/sec）。此差分
//     演算法抽成**獨立可測的自由函式**（`counter_rate` / `rates_from_delta` /
//     `usage_from_delta`），與來源、提供者解耦。
//   - **佇列深度為瞬時值**（非累積），不需差分——來源每次直接給當下深度，差分時原樣帶出。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：五個指標，各以**每磁碟**為可列舉實例：
//   - "disk.io.read_bytes"  name "Disk Read Throughput"  unit "B/s"   range at_least(0)
//   - "disk.io.write_bytes" name "Disk Write Throughput" unit "B/s"   range at_least(0)
//   - "disk.io.read_iops"   name "Disk Read IOPS"        unit "IOPS"  range at_least(0)
//   - "disk.io.write_iops"  name "Disk Write IOPS"       unit "IOPS"  range at_least(0)
//   - "disk.io.queue"       name "Disk Queue Depth"      unit ""      range at_least(0)
//   每個指標的**可列舉實例 = 每顆磁碟**（instance_id = 磁碟識別碼如 "disk0"，label 如
//   "Disk 0"）。各實例保留時序歷史（history_capacity>0），配合 E2-02 週期採集鋪成 IO 序列，
//   供折線 / sparkline 類元件直接鋪繪。吞吐率 / IOPS 上無界（at_least(0)）——不同儲存裝置
//   速率跨度極大，不強設上限。
#ifndef DS_MODULES_E2_07_DISK_IO_HPP
#define DS_MODULES_E2_07_DISK_IO_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 採集頻率分級（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// DiskIoCounters：單顆磁碟的**累積**計數 + 瞬時佇列深度（平台中立值）
// ---------------------------------------------------------------------------
// read_bytes / write_bytes / read_ops / write_ops 皆為**累積**（單調遞增）計數：單一時刻
// 的累積值本身無意義，速率須由兩次取樣差分得出（見 rates_from_delta）。queue_depth 則為
// **瞬時**值（當下未完成 IO 請求數），不需差分、差分時原樣帶出當前值。
struct DiskIoCounters {
    std::uint64_t read_bytes = 0;   // 累積已讀位元組
    std::uint64_t write_bytes = 0;  // 累積已寫位元組
    std::uint64_t read_ops = 0;     // 累積讀操作數
    std::uint64_t write_ops = 0;    // 累積寫操作數
    double queue_depth = 0.0;       // 瞬時佇列深度（未完成請求數；非累積）

    bool operator==(const DiskIoCounters& o) const {
        return read_bytes == o.read_bytes && write_bytes == o.write_bytes &&
               read_ops == o.read_ops && write_ops == o.write_ops &&
               queue_depth == o.queue_depth;
    }
    bool operator!=(const DiskIoCounters& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// DiskIoSnapshot：某顆磁碟的身分 + 累積計數（列舉單位）
// ---------------------------------------------------------------------------
// id 為磁碟穩定識別碼（如 "disk0"、"/dev/disk0"）、label 為人類可讀標籤（如 "Disk 0"）。
// 身分與計數分離，讓差分自由函式只對純數值運算、不觸及字串。
struct DiskIoSnapshot {
    std::string id;                 // 磁碟穩定識別碼
    std::string label;              // 人類可讀標籤
    DiskIoCounters counters;        // 該磁碟的累積計數 + 瞬時佇列

    bool operator==(const DiskIoSnapshot& o) const {
        return id == o.id && label == o.label && counters == o.counters;
    }
    bool operator!=(const DiskIoSnapshot& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// DiskIoSample：某一時刻**全磁碟**的累積計數快照（每顆磁碟一筆 + 時間戳）
// ---------------------------------------------------------------------------
// timestamp 為此快照的取樣時刻（秒；單調刻度即可，本層不認識真實時鐘）。速率 =
// 計數Δ / 時間戳差，故差分**需要時間差**（與 CPU 的 tick 比率不同——磁碟速率有量綱）。
// disks[i] = 第 i 顆磁碟的快照；磁碟數可在取樣間變動，差分時以兩快照的**共同磁碟數**
// （較小者）對齊（同 E2-03 CPU 每核對齊之先例）。
struct DiskIoSample {
    double timestamp = 0.0;              // 取樣時刻（秒；速率 = 計數Δ / 時間戳Δ）
    std::vector<DiskIoSnapshot> disks;   // 每顆磁碟一筆

    std::size_t disk_count() const noexcept { return disks.size(); }
    bool empty() const noexcept { return disks.empty(); }
};

// ---------------------------------------------------------------------------
// DiskIoRates：單顆磁碟的**已差分**速率 + 瞬時佇列深度（提供者面對的統一形狀）
// ---------------------------------------------------------------------------
// read_bps / write_bps 為吞吐率（bytes/sec）；read_iops / write_iops 為每秒操作數；
// queue_depth 為瞬時佇列深度（原樣自當前快照帶出，非速率）。
struct DiskIoRates {
    double read_bps = 0.0;    // 讀吞吐率（bytes/sec）
    double write_bps = 0.0;   // 寫吞吐率（bytes/sec）
    double read_iops = 0.0;   // 讀 IOPS（ops/sec）
    double write_iops = 0.0;  // 寫 IOPS（ops/sec）
    double queue_depth = 0.0; // 瞬時佇列深度（未完成請求數）

    bool operator==(const DiskIoRates& o) const {
        return read_bps == o.read_bps && write_bps == o.write_bps &&
               read_iops == o.read_iops && write_iops == o.write_iops &&
               queue_depth == o.queue_depth;
    }
    bool operator!=(const DiskIoRates& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// DiskIoReading：某顆磁碟的身分 + 已差分速率（列舉輸出單位）
// ---------------------------------------------------------------------------
struct DiskIoReading {
    std::string id;         // 磁碟穩定識別碼（自當前快照帶出）
    std::string label;      // 人類可讀標籤
    DiskIoRates rates;      // 該磁碟的已差分速率 + 瞬時佇列

    bool operator==(const DiskIoReading& o) const {
        return id == o.id && label == o.label && rates == o.rates;
    }
    bool operator!=(const DiskIoReading& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// DiskIoUsageSample：某一時刻**全磁碟**的 IO 速率快照（提供者的統一輸入形狀）
// ---------------------------------------------------------------------------
// disks[i] = 第 i 顆磁碟的速率讀值。valid==false 表「目前無讀值」（尚未取樣 / 差分僅一份 /
// 無共同磁碟 / 感測失敗）；消費者據此顯示為未知，而非把 0 誤當真實讀值。
struct DiskIoUsageSample {
    std::vector<DiskIoReading> disks;
    bool valid = false;

    std::size_t disk_count() const noexcept { return disks.size(); }

    // 明確的「無讀值」（保守預設）。
    static DiskIoUsageSample unknown() { return DiskIoUsageSample{}; }

    bool operator==(const DiskIoUsageSample& o) const {
        return valid == o.valid && disks == o.disks;
    }
    bool operator!=(const DiskIoUsageSample& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// 差分演算法（自由函式，獨立可測；平台中立純算術）
// ---------------------------------------------------------------------------
// 單一累積計數的速率 = (currΔ / dt)。防護：
//   - dt <= 0（無經過時間 / 時間戳未推進）→ 0（無從判斷速率，不謊報）。
//   - curr < prev（累積值理應單調；回繞 / 計數器重置）→ 0（保守，避免負值 / 爆量）。
double counter_rate(std::uint64_t prev, std::uint64_t curr, double dt) noexcept;

// 單顆磁碟差分：由兩份累積計數（prev→curr）+ 時間差 dt 算出四個速率；queue_depth 為
// **瞬時**值，直接取當前快照的深度（不差分）。
DiskIoRates rates_from_delta(const DiskIoCounters& prev, const DiskIoCounters& curr,
                             double dt) noexcept;

// 全磁碟差分：由兩份累積快照（prev→curr）算出每顆磁碟的速率讀值。
//   - dt = curr.timestamp - prev.timestamp（速率的時間基準）。
//   - 每顆磁碟：對齊到兩份的共同磁碟數（較小者），逐顆 rates_from_delta；身分（id/label）
//     取自**當前**快照。
//   - valid：共同磁碟數 >= 1 時為 true（有可差分的磁碟）；否則 false（無讀值）。
//   - dt <= 0 時仍 valid（有共同磁碟），惟各速率為 0、佇列深度照常帶出（瞬時值仍有意義）。
DiskIoUsageSample usage_from_delta(const DiskIoSample& prev, const DiskIoSample& curr);

// ---------------------------------------------------------------------------
// IoStatSource：**累積計數** 的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 列舉磁碟並給出各顆的累積 read/write bytes、累積 read/write ops、瞬時佇列深度 + 時間戳
// （尚未差分）。真實後端（相位 2+）於此讀 IOKit / `/proc/diskstats`；相位 1 只有注入式
// NullIoStatSource。差分邏輯不在此層——由 DifferencingIoRateSource 承擔（關注點分離，
// 兩者各自可測）。
class IoStatSource {
public:
    virtual ~IoStatSource() = default;

    // 讀一份目前的累積計數快照（含時間戳與各磁碟身分）。
    virtual DiskIoSample read() = 0;

protected:
    IoStatSource() = default;
    IoStatSource(const IoStatSource&) = default;
    IoStatSource& operator=(const IoStatSource&) = default;
};

// ---------------------------------------------------------------------------
// NullIoStatSource：相位 1 的 null / 假累積計數來源
// ---------------------------------------------------------------------------
// **不接任何真實 IO API**。持一列注入的累積計數快照（模擬時間推進下的累積值 + 時間戳），
// 每次 read() 回下一份；列盡則持續回最後一份（穩定，避免走出界）。空列預設回空快照。
class NullIoStatSource : public IoStatSource {
public:
    NullIoStatSource() = default;
    explicit NullIoStatSource(std::vector<DiskIoSample> sequence)
        : sequence_(std::move(sequence)) {}

    // 注入 / 覆寫整條累積計數序列（重置游標到起點）。
    void set_sequence(std::vector<DiskIoSample> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }
    // 追加一份累積計數快照到序列尾。
    void push_sample(DiskIoSample s) { sequence_.push_back(std::move(s)); }
    // 重置游標到序列起點。
    void reset() noexcept { cursor_ = 0; }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 回下一份累積計數快照；列盡回最後一份；空列回空快照。
    DiskIoSample read() override;

private:
    std::vector<DiskIoSample> sequence_;
    std::size_t cursor_ = 0;
};

// ---------------------------------------------------------------------------
// IoRateSource：**已差分速率** 的抽象來源（提供者只依賴此抽象）
// ---------------------------------------------------------------------------
// 每次 sample() 回一份 DiskIoUsageSample（各磁碟速率）。實作決定其來源（累積計數差分 /
// 直接注入 / 真實後端）。sample() **非 const**：差分型來源每次取樣會推進內部「上一份」
// 狀態，故取樣具副作用。
class IoRateSource {
public:
    virtual ~IoRateSource() = default;

    // 取一份目前 IO 速率快照。無讀值時回 DiskIoUsageSample::unknown()。
    virtual DiskIoUsageSample sample() = 0;

protected:
    IoRateSource() = default;
    IoRateSource(const IoRateSource&) = default;
    IoRateSource& operator=(const IoRateSource&) = default;
};

// ---------------------------------------------------------------------------
// NullIoRateSource：相位 1 的 null / 假「直接速率」來源
// ---------------------------------------------------------------------------
// **不接任何真實 IO API**。預設回「無讀值」（Mac / null 期的誠實預設）；可注入固定速率
// 快照供測試與假感測器情境（此即「來源直接給速率」路徑，繞過差分）。真實查詢留待後端相位。
class NullIoRateSource : public IoRateSource {
public:
    NullIoRateSource() = default;
    explicit NullIoRateSource(DiskIoUsageSample fixed) : fixed_(std::move(fixed)) {}

    // 注入 / 覆寫整份速率快照。
    void set_usage(DiskIoUsageSample usage) { fixed_ = std::move(usage); }

    // 回到「無讀值」預設（null 期誠實語意）。
    void clear() { fixed_ = DiskIoUsageSample::unknown(); }

    // 目前注入的快照（唯讀）。
    const DiskIoUsageSample& usage() const noexcept { return fixed_; }

    // 回目前注入的速率快照（決定性；無副作用，直接讀路徑）。
    DiskIoUsageSample sample() override { return fixed_; }

private:
    DiskIoUsageSample fixed_ = DiskIoUsageSample::unknown();
};

// ---------------------------------------------------------------------------
// DifferencingIoRateSource：把「累積計數來源」轉成「速率來源」的差分轉接器
// ---------------------------------------------------------------------------
// 消費一個 IoStatSource，內部保存「上一份」累積計數；每次 sample() 讀新一份、與上一份
// **差分**（usage_from_delta）得速率。首次 sample()（尚無上一份可比）回
// DiskIoUsageSample::unknown()（valid==false）——差分至少需兩份取樣。這正是「提供者以兩次
// 取樣差分算速率」的封裝，且本身是個乾淨可組合的 IoRateSource（提供者不必知道背後是差分或
// 直接速率）。
class DifferencingIoRateSource : public IoRateSource {
public:
    explicit DifferencingIoRateSource(std::shared_ptr<IoStatSource> stats)
        : stats_(std::move(stats)) {}

    // 是否已有「上一份」基準（即已至少取樣一次）。
    bool primed() const noexcept { return primed_; }

    // 丟棄已保存的基準（下一次 sample() 又回「需兩份」語意）。
    void reset() noexcept { primed_ = false; prev_ = DiskIoSample{}; }

    // 讀新一份累積計數、與上一份差分得速率。首次（或 reset 後首次）回 unknown。
    DiskIoUsageSample sample() override;

private:
    std::shared_ptr<IoStatSource> stats_;
    DiskIoSample prev_;
    bool primed_ = false;
};

// ---------------------------------------------------------------------------
// DiskIoProvider：把磁碟 IO 速率掛成一組指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上五個指標（讀 / 寫
// 吞吐、讀 / 寫 IOPS、佇列深度），每個指標的可列舉實例 = 每顆磁碟。因速率會隨時間變動，
// 本提供者以 **E2-02 的週期分級**採集（磁碟 IO 屬常規頻率，預設 Normal）：呼叫端把各
// metric_id 與 sampling_tier() 登記到 SamplingScheduler，於排程器判定該採集時呼叫 sample()
// 重新讀來源並更新各實例（速率推入歷史）。消費者（掛件）只透過 E2-01 的 MetricRegistry /
// Metric 介面走訪，完全不觸及本型別。
class DiskIoProvider : public ds::metrics::MetricProvider {
public:
    static constexpr const char* kProviderId = "sysinfo.disk_io";

    // 五個指標的識別碼 / 顯示名 / 單位。
    static constexpr const char* kReadBytesId = "disk.io.read_bytes";
    static constexpr const char* kWriteBytesId = "disk.io.write_bytes";
    static constexpr const char* kReadIopsId = "disk.io.read_iops";
    static constexpr const char* kWriteIopsId = "disk.io.write_iops";
    static constexpr const char* kQueueId = "disk.io.queue";

    static constexpr const char* kReadBytesName = "Disk Read Throughput";
    static constexpr const char* kWriteBytesName = "Disk Write Throughput";
    static constexpr const char* kReadIopsName = "Disk Read IOPS";
    static constexpr const char* kWriteIopsName = "Disk Write IOPS";
    static constexpr const char* kQueueName = "Disk Queue Depth";

    static constexpr const char* kBytesUnit = "B/s";   // 吞吐率
    static constexpr const char* kIopsUnit = "IOPS";   // 每秒操作數
    static constexpr const char* kQueueUnit = "";      // 佇列深度（無量綱計數）

    // 各實例歷史環的預設容量（配合 E2-02 週期採集鋪成 IO 序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：磁碟 IO 屬常規頻率（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Normal;

    // 以一個 IO 速率來源建構。source 為 null 時，提供者仍會掛上五個指標，且皆無磁碟實例
    // （保守而不崩、不謊報 0）。history 為各實例歷史環容量、tier 為建議採集分級。
    explicit DiskIoProvider(std::shared_ptr<IoRateSource> source,
                            std::size_t history = kDefaultHistory,
                            ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 對註冊表掛上五個磁碟 IO 指標：取一份速率、依其磁碟建立每磁碟實例、填初值，並保留
    // 指標參照供日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新速率寫入各實例（速率推入歷史）。呼叫端在 E2-02 排程器判定本組指標
    // 該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    // 若新取樣出現既有未見的磁碟，會**動態新增**該磁碟實例到五個指標（既有參照不失效）；
    // 若某既有磁碟本次未出現，其實例設為未知（不污染歷史）。
    void sample();

    // 目前的磁碟實例數（每個指標的實例數相同）。register_metrics 前為 0。
    std::size_t disk_count() const noexcept { return slots_.size(); }

private:
    // 每顆磁碟在五個指標中對應的實例指標集合。
    struct DiskSlot {
        std::string id;
        ds::metrics::InMemoryMetricInstance* read_bytes = nullptr;
        ds::metrics::InMemoryMetricInstance* write_bytes = nullptr;
        ds::metrics::InMemoryMetricInstance* read_iops = nullptr;
        ds::metrics::InMemoryMetricInstance* write_iops = nullptr;
        ds::metrics::InMemoryMetricInstance* queue = nullptr;
    };

    // 目前速率快照：source_ 為 null 時視為「無讀值」。
    DiskIoUsageSample current() {
        return source_ ? source_->sample() : DiskIoUsageSample::unknown();
    }

    // 把一份速率快照寫入各實例。to_history 為真時速率推入歷史（採集路徑）。
    // 必要時動態新增磁碟實例；本次未出現的既有磁碟設為未知（不污染歷史）。
    void apply(const DiskIoUsageSample& usage, bool to_history);

    // 找既有磁碟槽或新建（於五個指標各 add_instance）。回傳槽索引。
    std::size_t ensure_slot(const std::string& id, const std::string& label);

    // 把一個數值寫入某實例（valid 決定是否為未知 / 是否推歷史）。
    void write_value(ds::metrics::InMemoryMetricInstance* inst, double value, bool valid,
                     bool to_history);

    std::shared_ptr<IoRateSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有五個指標（與 registry 共享同一物件，故更新對消費者可見）。
    std::shared_ptr<ds::metrics::InMemoryMetric> read_bytes_metric_;
    std::shared_ptr<ds::metrics::InMemoryMetric> write_bytes_metric_;
    std::shared_ptr<ds::metrics::InMemoryMetric> read_iops_metric_;
    std::shared_ptr<ds::metrics::InMemoryMetric> write_iops_metric_;
    std::shared_ptr<ds::metrics::InMemoryMetric> queue_metric_;

    // 磁碟識別碼 → slots_ 索引（多磁碟去重 + 動態新增用）。
    std::vector<DiskSlot> slots_;
    std::unordered_map<std::string, std::size_t> slot_index_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_07_DISK_IO_HPP
