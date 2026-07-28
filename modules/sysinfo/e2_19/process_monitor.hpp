// E2-19 行程監看 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「執行中行程列表」（每個行程的 pid / 名稱 / CPU% / 記憶體用量 / 狀態）透過
// **E2-01 的 MetricProvider 介面**掛成一個指標，支援 **top-N 排序**（依 CPU 或記憶體），
// 並以 **E2-02 的採集頻率分級**決定採樣節奏。這是「新增指標 = 新增 MetricProvider、
// 掛件一行不動」機制的又一個具體提供者——它**消費 E2-01 / E2-02 契約、不自造指標模型
// 或排程器**。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實行程 API**（無 `sysctl` /
//     `/proc` / `EnumProcesses` / `#ifdef` / win32 / cocoa）。真實後端（相位 2+）另實作
//     抽象來源 `ProcessSource`，提供者一行不動。
//   - 行程列表隨時間變動（起／停），故本層以**可注入的 ProcessSource 抽象**（列舉行程 +
//     每行程統計）+ **null / 假來源**（注入行程清單 / 序列）表達，完全可單元測試。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - 排序維度 = CPU 時：id="proc.cpu"、name="Process CPU"、unit="%"、range=bounded[0,100]。
//   - 排序維度 = 記憶體時：id="proc.memory"、name="Process Memory"、unit="MB"、
//     range=at_least(0)。
//   - **可列舉實例 = top-N 個行程**（依排序維度由高到低取前 N；top_n==0 = 全部）：
//       每個行程一個 `MetricInstance`——instance_id = pid（穩定識別碼）、label = 行程名、
//       value.number = 排序維度值（CPU% 或記憶體 MB）、value.text = 狀態字串（surface 狀態）、
//       各實例保留時序歷史（配合 E2-02 週期採集鋪成負載序列）。
//     完整的每行程五欄位（pid / 名稱 / CPU% / 記憶體 / 狀態）保存於 `ProcessInfo`，可經
//     `processes()` 取得（排序後的 top-N，含未經排序維度的另一欄）。
#ifndef DS_MODULES_E2_19_PROCESS_MONITOR_HPP
#define DS_MODULES_E2_19_PROCESS_MONITOR_HPP

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
// ProcessStatus：行程狀態的平台中立列舉
// ---------------------------------------------------------------------------
// 跨平台一致的最小狀態集合（各 OS 的原生狀態碼在後端相位映射到此）。刻意不含平台專屬
// 狀態，維持 module 層平台中立。
enum class ProcessStatus {
    Running,   // 執行中 / 可執行
    Sleeping,  // 睡眠 / 等待
    Stopped,   // 已停止（暫停）
    Zombie,    // 殭屍（已結束待回收）
    Unknown,   // 未知（未讀到 / 無對應）
};

// 診斷 / 顯示用穩定字串（"running"/"sleeping"/"stopped"/"zombie"/"unknown"）。
const char* to_string(ProcessStatus status) noexcept;

// ---------------------------------------------------------------------------
// ProcessSortKey：top-N 的排序維度
// ---------------------------------------------------------------------------
// 依 CPU 使用率或記憶體用量排序取前 N。決定了暴露指標的值維度（見類註）。
enum class ProcessSortKey {
    Cpu,     // 依 CPU% 由高到低
    Memory,  // 依記憶體用量由高到低
};

// ---------------------------------------------------------------------------
// ProcessInfo：單一執行中行程的平台中立描述（五欄位）
// ---------------------------------------------------------------------------
// 跨平台一致的最小描述。刻意不含平台專屬欄位（可執行檔路徑 / 使用者 / 執行緒數等），
// 維持 module 層平台中立。
struct ProcessInfo {
    std::uint64_t pid = 0;                          // 行程識別碼（穩定於行程存活期間）
    std::string name;                               // 人類可讀行程名（如 "WindowServer"）
    double cpu_percent = 0.0;                       // CPU 使用率 %（[0,100]）
    std::uint64_t memory_bytes = 0;                 // 常駐記憶體用量（bytes）
    ProcessStatus status = ProcessStatus::Unknown;  // 行程狀態

    bool operator==(const ProcessInfo& o) const {
        return pid == o.pid && name == o.name && cpu_percent == o.cpu_percent &&
               memory_bytes == o.memory_bytes && status == o.status;
    }
    bool operator!=(const ProcessInfo& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// ProcessSample：某一時刻的行程列表快照
// ---------------------------------------------------------------------------
// 提供者面對的統一形狀：
//   - processes  目前執行中的行程（順序即來源列舉順序，尚未排序）。
//   - valid      false = 「目前無讀值」（尚未取樣 / 感測失敗）。消費者據此顯示為未知，
//                而非把空列表誤當成「沒有任何行程」的真實讀值。
struct ProcessSample {
    std::vector<ProcessInfo> processes;
    bool valid = false;

    // 明確的「無讀值」（保守預設）。
    static ProcessSample unknown() { return ProcessSample{}; }

    std::size_t size() const noexcept { return processes.size(); }
    bool empty() const noexcept { return processes.empty(); }

    bool operator==(const ProcessSample& o) const {
        return valid == o.valid && processes == o.processes;
    }
    bool operator!=(const ProcessSample& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// 排序 / top-N（自由函式，獨立可測；平台中立純邏輯）
// ---------------------------------------------------------------------------
// 某行程在給定排序維度下的排序值：Cpu → cpu_percent；Memory → memory_bytes（原始 bytes，
// 供排序比較；暴露為指標時再換算 MB，見 ProcessMonitorProvider::kBytesPerMiB）。
double process_sort_value(const ProcessInfo& p, ProcessSortKey key) noexcept;

// 依排序維度由高到低取前 top_n 個行程（top_n==0 = 全部）。同值以 pid 遞增為次序（決定性，
// 穩定輸出供測試與穩定 UI）。回傳新的 vector（不改動輸入）。
std::vector<ProcessInfo> top_processes(const std::vector<ProcessInfo>& procs,
                                       ProcessSortKey key, std::size_t top_n);

// ---------------------------------------------------------------------------
// ProcessSource：執行中行程列表的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 sample() 回一份 ProcessSample。實作決定其來源（注入假資料 /
// 真實後端）。sample() **非 const**：序列型來源每次取樣會推進內部游標，故取樣具副作用。
// 真實後端（相位 2+）於此讀 sysctl / /proc / EnumProcesses；相位 1 只有 NullProcessSource。
class ProcessSource {
public:
    virtual ~ProcessSource() = default;

    // 取一份目前行程列表快照。無讀值時回 ProcessSample::unknown()。
    virtual ProcessSample sample() = 0;

protected:
    ProcessSource() = default;
    ProcessSource(const ProcessSource&) = default;
    ProcessSource& operator=(const ProcessSource&) = default;
};

// ---------------------------------------------------------------------------
// NullProcessSource：相位 1 的 null / 假來源
// ---------------------------------------------------------------------------
// **不接任何真實行程 API**。預設回「無讀值」（Mac / null 期的誠實預設）；可注入固定快照
// 或**一列快照序列**（模擬時間推進下行程起／停），供測試與假感測器情境。真實列舉留待
// 後端相位——本類永不含平台呼叫。
//
// 序列語意（與 E2-03 NullCpuTickSource 同風格）：每次 sample() 回序列的下一份；列盡則
// 持續回最後一份（穩定，不走出界）；空序列回「無讀值」。
class NullProcessSource : public ProcessSource {
public:
    NullProcessSource() = default;

    // 以單一固定快照建構（valid==true）。之後每次 sample() 皆回此快照。
    explicit NullProcessSource(std::vector<ProcessInfo> processes) {
        sequence_.push_back(std::move(processes));
    }

    // 注入 / 覆寫為單一固定快照（valid==true；重置游標）。
    void set_processes(std::vector<ProcessInfo> processes) {
        sequence_.clear();
        sequence_.push_back(std::move(processes));
        cursor_ = 0;
    }

    // 注入 / 覆寫整條快照序列（重置游標到起點）。序列中每份皆視為 valid。
    void set_sequence(std::vector<std::vector<ProcessInfo>> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }

    // 追加一份快照到序列尾。
    void push_snapshot(std::vector<ProcessInfo> processes) {
        sequence_.push_back(std::move(processes));
    }

    // 回到「無讀值」預設（清空序列 → sample() 回 unknown）。
    void clear() {
        sequence_.clear();
        cursor_ = 0;
    }

    // 重置游標到序列起點。
    void reset() noexcept { cursor_ = 0; }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 回下一份快照（valid==true）；列盡回最後一份；空序列回 unknown。
    ProcessSample sample() override;

private:
    std::vector<std::vector<ProcessInfo>> sequence_;  // 注入的快照序列
    std::size_t cursor_ = 0;                          // 下次 sample() 讀取位置
};

// ---------------------------------------------------------------------------
// ProcessMonitorProvider：把 top-N 行程掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標（id 依排序
// 維度：proc.cpu / proc.memory），其可列舉實例 = top-N 行程（每行程一實例，pid 為 id）。
// 因行程列表隨時間變動，本提供者宜以 **E2-02 的週期分級**採集（預設 Normal）：呼叫端把
// metric_id() 與 sampling_tier() 登記到 SamplingScheduler，於排程器判定該採集時呼叫
// sample() 重新讀來源並更新各實例（排序維度值推入歷史）。消費者（掛件）只透過 E2-01 的
// MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class ProcessMonitorProvider : public ds::metrics::MetricProvider {
public:
    // 提供者穩定識別碼（供診斷 / 去重 / 溯源）。
    static constexpr const char* kProviderId = "sysinfo.proc";

    // 兩種排序維度對應的指標身分。
    static constexpr const char* kCpuMetricId = "proc.cpu";
    static constexpr const char* kMemoryMetricId = "proc.memory";
    static constexpr const char* kCpuMetricName = "Process CPU";
    static constexpr const char* kMemoryMetricName = "Process Memory";
    static constexpr const char* kCpuUnit = "%";
    static constexpr const char* kMemoryUnit = "MB";

    // 記憶體 bytes → MB 的換算（暴露為指標值時用；排序仍以原始 bytes 比較）。
    static constexpr double kBytesPerMiB = 1024.0 * 1024.0;

    // top_n == 0 表「全部行程」（不截斷）。
    static constexpr std::size_t kAllProcesses = 0;

    // 各實例歷史環的預設容量（配合 E2-02 週期採集鋪成序列）。
    static constexpr std::size_t kDefaultHistory = 64;

    // 建議採集分級：行程列表變動速度常規，屬 Normal（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Normal;

    // 以一個行程來源建構。source 為 null 時，提供者仍會掛上指標（無實例，保守而不崩）。
    // sort_key 決定排序維度與指標身分；top_n 為取前幾名（0=全部）；history 為各實例歷史環
    // 容量；tier 為建議採集分級。
    explicit ProcessMonitorProvider(std::shared_ptr<ProcessSource> source,
                                    ProcessSortKey sort_key = ProcessSortKey::Cpu,
                                    std::size_t top_n = kAllProcesses,
                                    std::size_t history = kDefaultHistory,
                                    ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)),
          sort_key_(sort_key),
          top_n_(top_n),
          history_(history),
          tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者掛上的指標識別碼（依排序維度）。
    ds::metrics::MetricId metric_id() const {
        return sort_key_ == ProcessSortKey::Cpu ? kCpuMetricId : kMemoryMetricId;
    }

    // 排序維度 / top-N 上限 / 建議採集分級（供呼叫端 add_demand 用）。
    ProcessSortKey sort_key() const noexcept { return sort_key_; }
    std::size_t top_n() const noexcept { return top_n_; }
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 對註冊表掛上指標：取一份行程列表、排序取 top-N、每行程建一實例、填初值，並保留指標
    // 參照供日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、排序取 top-N、把新值寫入各實例（排序維度值推入歷史）。呼叫端在 E2-02
    // 排程器判定本指標該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    // 新出現於 top-N 的 pid 會**動態新增**實例（既有參照不失效）；本次未出現的既有 pid
    // 設為未知（不污染歷史，誠實表達「本次無讀值」）。
    void sample();

    // 目前暴露的行程實例數（register_metrics 前為 0）。
    std::size_t process_count() const noexcept { return order_.size(); }

    // 最近一次採樣的 top-N 行程（已排序，含完整五欄位）。無讀值時為空。
    const std::vector<ProcessInfo>& processes() const noexcept { return last_top_; }

    // 最近一次採樣是否有讀值。
    bool has_reading() const noexcept { return has_reading_; }

private:
    // 目前行程列表快照：source_ 為 null 時視為「無讀值」。
    ProcessSample current() {
        return source_ ? source_->sample() : ProcessSample::unknown();
    }

    // 把一份行程列表快照套用到指標：排序取 top-N、更新 / 新增實例、缺席者設未知。
    // to_history 為真時排序維度值推入歷史（採集路徑）。
    void apply(const ProcessSample& sample, bool to_history);

    // 某行程在本提供者排序維度下的暴露值（Cpu → %；Memory → MB）。
    double exposed_value(const ProcessInfo& p) const noexcept;

    std::shared_ptr<ProcessSource> source_;
    ProcessSortKey sort_key_;
    std::size_t top_n_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有，供 sample() 更新（與 registry 共享同一物件，故更新對消費者
    // 可見）。非擁有指標指向 metric_ 內的實例（其壽命由 metric_ 保證，unique_ptr 持有，
    // 故新增更多實例時既有參照不失效）。
    std::shared_ptr<ds::metrics::InMemoryMetric> metric_;
    std::unordered_map<std::uint64_t, ds::metrics::InMemoryMetricInstance*> inst_by_pid_;
    std::vector<std::uint64_t> order_;   // 實例建立順序（pid），決定性走訪
    std::vector<ProcessInfo> last_top_;  // 最近一次的排序後 top-N（完整欄位）
    bool has_reading_ = false;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_19_PROCESS_MONITOR_HPP
