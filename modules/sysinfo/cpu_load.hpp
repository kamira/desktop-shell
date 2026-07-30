// E2-03 CPU 負載（總體與每核心）— sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「CPU 使用率」（總體 %＋每個邏輯核心 %）透過 **E2-01 的 MetricProvider 介面**
// 掛成一個指標，並以 **E2-02 的採集頻率分級** 決定採樣節奏。這是「新增指標 = 新增
// MetricProvider、掛件一行不動」機制的又一個具體提供者——它**消費 E2-01 / E2-02 契約、
// 不自造指標模型或排程器**。本單元是最終「CPU / GPU Usage Widget」的核心資料來源，故
// 介面刻意乾淨、易組合（差分邏輯與提供者分離、來源可注入、來源可鏈接）。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實 CPU 統計 API**（無
//     `host_processor_info` / `/proc/stat` / mach / `#ifdef` / win32 / cocoa）。真實後端
//     （相位 2+）另實作抽象來源，提供者一行不動。
//   - 使用率的兩條計算路徑，皆平台中立、可注入、可完全單元測試：
//       * **累積 tick 差分**：來源給各核心的**累積** busy / total tick；提供者以「兩次
//         取樣差分」算使用率 = (busyΔ / totalΔ)。此路徑由 `DifferencingCpuStatSource`
//         封裝（把一個 tick 來源轉成使用率來源），差分演算法另以自由函式 `usage_from_delta`
//         提供、獨立可測。
//       * **直接比率**：來源直接給各核心使用率比率 [0,1]（某些 API 給瞬時 %）；
//         `NullCpuStatSource` 即此路徑的 null / 假來源。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id    = "cpu.usage"
//   - name  = "CPU Usage"
//   - unit  = "%"
//   - range = bounded [0,100]（使用率百分比，可正規化到 [0,1] 供長條 / 直方圖類元件）
//   - **可列舉實例 = 一個總體 + 每個邏輯核心**（此即單元名「總體與每核心」在 E2-01
//     「可列舉實例」要素上的自然對應）：
//       * "total"  — label "CPU Total"，總體使用率 %
//       * "cpu0" / "cpu1" / … — label "Core 0" / "Core 1" / …，各核心使用率 %
//     各實例保留時序歷史（history_capacity>0），配合 E2-02 週期採集鋪成負載序列，供
//     折線 / sparkline 類元件直接鋪繪。
#ifndef DS_MODULES_E2_03_CPU_LOAD_HPP
#define DS_MODULES_E2_03_CPU_LOAD_HPP

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
// CpuCoreTicks：單一邏輯核心的**累積** tick 計數（平台中立值）
// ---------------------------------------------------------------------------
// 作業系統原生的 CPU 統計多為單調遞增的累積 tick（macOS host_processor_info、Linux
// /proc/stat 皆然）。此處只描述其平台中立形狀，**不含任何取值方式**：
//   - busy  累積「非閒置」tick（user + system + nice + …）。
//   - total 累積「全部」tick（busy + idle）。
// 使用率須由**兩次取樣差分**得出（見 usage_from_delta）：單一時刻的累積值本身無意義。
struct CpuCoreTicks {
    std::uint64_t busy = 0;   // 累積非閒置 tick
    std::uint64_t total = 0;  // 累積全部 tick（busy + idle）

    bool operator==(const CpuCoreTicks& o) const {
        return busy == o.busy && total == o.total;
    }
    bool operator!=(const CpuCoreTicks& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// CpuTicksSample：某一時刻**全 CPU** 的累積 tick 快照（每核心一筆）
// ---------------------------------------------------------------------------
// cores[i] = 第 i 個邏輯核心的累積 tick。核心數 = cores.size()，可在取樣間變動
// （熱插拔 / 動態上下線）；差分時以兩快照的**共同核心數**（較小者）對齊。
struct CpuTicksSample {
    std::vector<CpuCoreTicks> cores;

    std::size_t core_count() const noexcept { return cores.size(); }
    bool empty() const noexcept { return cores.empty(); }
};

// ---------------------------------------------------------------------------
// CpuUsageSample：某一時刻的 CPU 使用率快照（比率 [0,1]，非百分比）
// ---------------------------------------------------------------------------
// 提供者面對的統一使用率形狀（無論來自 tick 差分或直接比率）：
//   - overall   總體使用率比率 [0,1]。
//   - per_core  各邏輯核心使用率比率 [0,1]（per_core[i] = 第 i 核）。
//   - valid     false = 「目前無讀值」（尚未取樣 / 差分僅一份 / 感測失敗）。消費者據此
//               顯示為未知，而非把 0 誤當真實讀值。
// 比率而非百分比：差分自然得 [0,1]；提供者掛指標時再乘 100 成 %（見 kPercentScale）。
struct CpuUsageSample {
    double overall = 0.0;
    std::vector<double> per_core;
    bool valid = false;

    std::size_t core_count() const noexcept { return per_core.size(); }

    // 明確的「無讀值」（保守預設）。
    static CpuUsageSample unknown() { return CpuUsageSample{}; }

    bool operator==(const CpuUsageSample& o) const {
        return valid == o.valid && overall == o.overall && per_core == o.per_core;
    }
    bool operator!=(const CpuUsageSample& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// 差分演算法（自由函式，獨立可測；平台中立純算術）
// ---------------------------------------------------------------------------
// 單核差分：使用率比率 = (busyΔ / totalΔ)，夾到 [0,1]。
//   - totalΔ <= 0（無經過時間）→ 0（無從判斷，不謊報）。
//   - 計數器重置（curr < prev，累積值理應單調）→ 視為 0（保守，避免負值 / 爆量）。
//   - busyΔ > totalΔ（理論不該發生）→ 夾到 1.0。
double core_usage_ratio(const CpuCoreTicks& prev, const CpuCoreTicks& curr) noexcept;

// 全 CPU 差分：由兩份累積快照（prev→curr）算出每核心 + 總體使用率比率。
//   - 每核心：對齊到兩份的共同核心數（較小者），逐核 core_usage_ratio。
//   - 總體：以**聚合差分** sum(busyΔ) / sum(totalΔ)（非各核比率的算術平均——各核 totalΔ
//     可能不同，聚合差分才正確）。
//   - valid：共同核心數 >= 1 時為 true（有可差分的核心）；否則 false（無讀值）。
CpuUsageSample usage_from_delta(const CpuTicksSample& prev, const CpuTicksSample& curr);

// ---------------------------------------------------------------------------
// CpuStatSource：CPU 使用率的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 sample() 回一份 CpuUsageSample（比率 [0,1]）。實作決定其來源
// （直接比率 / tick 差分 / 真實後端）。sample() **非 const**：差分型來源每次取樣會推進
// 內部「上一份」狀態，故取樣具副作用。
class CpuStatSource {
public:
    virtual ~CpuStatSource() = default;

    // 取一份目前使用率快照（比率 [0,1]）。無讀值時回 CpuUsageSample::unknown()。
    virtual CpuUsageSample sample() = 0;

protected:
    CpuStatSource() = default;
    CpuStatSource(const CpuStatSource&) = default;
    CpuStatSource& operator=(const CpuStatSource&) = default;
};

// ---------------------------------------------------------------------------
// NullCpuStatSource：相位 1 的 null / 假「直接比率」來源
// ---------------------------------------------------------------------------
// **不接任何真實 CPU API**。預設回「無讀值」（Mac / null 期的誠實預設）；可注入固定 /
// 序列使用率比率供測試與假感測器情境（此即「來源直接給比率」路徑）。真實查詢留待後端
// 相位——本類永不含平台呼叫。
class NullCpuStatSource : public CpuStatSource {
public:
    NullCpuStatSource() = default;
    explicit NullCpuStatSource(CpuUsageSample fixed) : fixed_(std::move(fixed)) {}

    // 注入 / 覆寫整份使用率快照（比率 [0,1]）。
    void set_usage(CpuUsageSample usage) { fixed_ = std::move(usage); }

    // 便利：以每核心比率設定；總體 = 各核比率算術平均（等核心數取樣的正確近似）。
    void set_per_core(std::vector<double> ratios);

    // 回到「無讀值」預設（null 期誠實語意）。
    void clear() { fixed_ = CpuUsageSample::unknown(); }

    // 目前注入的快照（唯讀）。
    const CpuUsageSample& usage() const noexcept { return fixed_; }

    // 回目前注入的使用率快照（決定性；無副作用，直接讀路徑）。
    CpuUsageSample sample() override { return fixed_; }

private:
    CpuUsageSample fixed_ = CpuUsageSample::unknown();
};

// ---------------------------------------------------------------------------
// CpuTickSource：**累積 tick** 的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 給出各核心的累積 busy / total tick 快照（尚未差分）。真實後端（相位 2+）於此讀
// host_processor_info / /proc/stat；相位 1 只有注入式 NullCpuTickSource。差分邏輯不在
// 此層——由 DifferencingCpuStatSource 承擔（關注點分離，兩者各自可測）。
class CpuTickSource {
public:
    virtual ~CpuTickSource() = default;

    // 讀一份目前的累積 tick 快照。
    virtual CpuTicksSample read() = 0;

protected:
    CpuTickSource() = default;
    CpuTickSource(const CpuTickSource&) = default;
    CpuTickSource& operator=(const CpuTickSource&) = default;
};

// ---------------------------------------------------------------------------
// NullCpuTickSource：相位 1 的 null / 假 tick 來源
// ---------------------------------------------------------------------------
// **不接任何真實 CPU API**。持一列注入的累積 tick 快照（模擬時間推進下的累積值），每次
// read() 回下一份；列盡則持續回最後一份（穩定，避免走出界）。空列預設回空快照。
class NullCpuTickSource : public CpuTickSource {
public:
    NullCpuTickSource() = default;
    explicit NullCpuTickSource(std::vector<CpuTicksSample> sequence)
        : sequence_(std::move(sequence)) {}

    // 注入 / 覆寫整條累積 tick 序列（重置游標到起點）。
    void set_sequence(std::vector<CpuTicksSample> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }
    // 追加一份累積 tick 快照到序列尾。
    void push_sample(CpuTicksSample s) { sequence_.push_back(std::move(s)); }
    // 重置游標到序列起點。
    void reset() noexcept { cursor_ = 0; }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 回下一份累積 tick 快照；列盡回最後一份；空列回空快照。
    CpuTicksSample read() override;

private:
    std::vector<CpuTicksSample> sequence_;
    std::size_t cursor_ = 0;
};

// ---------------------------------------------------------------------------
// DifferencingCpuStatSource：把「累積 tick 來源」轉成「使用率來源」的差分轉接器
// ---------------------------------------------------------------------------
// 消費一個 CpuTickSource，內部保存「上一份」累積 tick；每次 sample() 讀新一份、與上一份
// **差分**（usage_from_delta）得使用率比率。首次 sample()（尚無上一份可比）回
// CpuUsageSample::unknown()（valid==false）——差分至少需兩份取樣。這正是「提供者以兩次
// 取樣差分算使用率」的封裝，且本身是個乾淨可組合的 CpuStatSource（提供者不必知道背後是
// 差分或直接比率）。
class DifferencingCpuStatSource : public CpuStatSource {
public:
    explicit DifferencingCpuStatSource(std::shared_ptr<CpuTickSource> ticks)
        : ticks_(std::move(ticks)) {}

    // 是否已有「上一份」基準（即已至少取樣一次）。
    bool primed() const noexcept { return primed_; }

    // 丟棄已保存的基準（下一次 sample() 又回「需兩份」語意）。
    void reset() noexcept { primed_ = false; prev_ = CpuTicksSample{}; }

    // 讀新一份累積 tick、與上一份差分得使用率。首次（或 reset 後首次）回 unknown。
    CpuUsageSample sample() override;

private:
    std::shared_ptr<CpuTickSource> ticks_;
    CpuTicksSample prev_;
    bool primed_ = false;
};

// ---------------------------------------------------------------------------
// CpuLoadProvider：把 CPU 使用率掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標
// "cpu.usage"，其可列舉實例 = 一個總體 + 每邏輯核心。因使用率會隨時間變動，本提供者
// 建議以 **E2-02 的週期分級**採集（CPU 負載宜跟手，預設 High）：呼叫端把 metric_id 與
// sampling_tier() 登記到 SamplingScheduler，於排程器判定該採集時呼叫 sample() 重新
// 讀來源並更新各實例（使用率推入歷史）。消費者（掛件）只透過 E2-01 的
// MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class CpuLoadProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼 / 顯示名 / 單位。
    static constexpr const char* kMetricId = "cpu.usage";
    static constexpr const char* kProviderId = "sysinfo.cpu";
    static constexpr const char* kMetricName = "CPU Usage";
    static constexpr const char* kUnit = "%";

    // 總體實例的穩定識別碼與顯示名。
    static constexpr const char* kInstanceTotal = "total";
    static constexpr const char* kTotalLabel = "CPU Total";

    // 使用率比率 [0,1] → 百分比 % 的縮放。
    static constexpr double kPercentScale = 100.0;

    // 各實例歷史環的預設容量（配合 E2-02 週期採集鋪成負載序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：CPU 負載宜跟手，屬高頻（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::High;

    // 以一個 CPU 使用率來源建構。source 為 null 時，提供者仍會掛上指標，且總體實例以
    // 「未知」（valid==false）呈現、無核心實例（保守而不崩、不謊報 0）。history 為各實例
    // 歷史環容量、tier 為建議採集分級。
    explicit CpuLoadProvider(std::shared_ptr<CpuStatSource> source,
                             std::size_t history = kDefaultHistory,
                             ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 對註冊表掛上 "cpu.usage" 指標：取一份使用率、建總體 + 每核心實例、填初值，並保留
    // 指標參照供日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新使用率寫入各實例（使用率推入歷史）。呼叫端在 E2-02 排程器判定
    // 本指標該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    // 若新取樣的核心數多於既有核心實例，會**動態新增**核心實例（既有參照不失效）；
    // 若較少，多出的核心實例設為未知（不污染歷史）。
    void sample();

    // 目前的邏輯核心實例數（不含總體實例）。register_metrics 前為 0。
    std::size_t core_count() const noexcept { return core_insts_.size(); }

private:
    // 把一份使用率寫入各實例。to_history 為真時使用率推入歷史（採集路徑），
    // 為假時只設值不動歷史。必要時動態擴增核心實例。
    void apply(const CpuUsageSample& usage, bool to_history);

    // 目前使用率快照：source_ 為 null 時視為「無讀值」。
    CpuUsageSample current() {
        return source_ ? source_->sample() : CpuUsageSample::unknown();
    }

    // 把一個比率 [0,1] 寫入某實例（乘 100 成 %；valid 決定是否為未知 / 是否推歷史）。
    void write_ratio(ds::metrics::InMemoryMetricInstance* inst, double ratio, bool valid,
                     bool to_history);

    std::shared_ptr<CpuStatSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有，供 sample() 更新（與 registry 共享同一物件，故更新對
    // 消費者可見）。非擁有指標指向 metric_ 內的實例（其壽命由 metric_ 保證，unique_ptr
    // 持有，故新增更多核心實例時既有參照不失效）。
    std::shared_ptr<ds::metrics::InMemoryMetric> metric_;
    ds::metrics::InMemoryMetricInstance* inst_total_ = nullptr;
    std::vector<ds::metrics::InMemoryMetricInstance*> core_insts_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_03_CPU_LOAD_HPP
