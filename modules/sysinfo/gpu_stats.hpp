// E2-05 GPU 使用率 / VRAM / 溫度 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「GPU 指標」——使用率(%)、VRAM 用量(已用/總量 bytes，主維度為 %)、溫度(°C)
// ——透過 **E2-01 的 MetricProvider 介面**掛成三個指標，並以 **E2-02 的採集頻率分級**
// 決定採樣節奏。支援**多 GPU**：每張 GPU 為每個指標貢獻一個可列舉實例（gpu0 / gpu1 / …）。
// 這是「新增指標 = 新增 MetricProvider、掛件一行不動」機制的又一個具體提供者——它
// **消費 E2-01 / E2-02 契約、不自造指標模型或排程器**。本單元是最終「CPU / GPU Usage
// Widget」的 **GPU 資料源**，故介面刻意乾淨、易組合（來源可注入、值模型平台中立）。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實 GPU API**（無 Metal / IOKit /
//     nvml / `#ifdef` / win32 / cocoa）。真實後端（相位 2+）另實作抽象來源 `GpuStatSource`，
//     提供者一行不動。
//   - 來源以**可注入的 GpuStatSource 抽象**表達：一次 sample() 列舉所有 GPU，每張 GPU 給
//     usage / vram(used,total) / temp 三組讀值，各自帶 valid 旗標（可獨立缺讀）。
//     相位 1 只有 `NullGpuStatSource`（注入固定 / 序列資料），預設回「無讀值」。
//   - **無讀值誠實回 invalid、不謊報 0**：某欄位無讀值即以 E2-01 `MetricValue` 的
//     valid==false（未知）表達，消費者據此顯示為未知，而非把 0 誤當真實讀值。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - "gpu.usage" — name "GPU Usage"、unit "%"、range bounded[0,100]
//   - "gpu.vram"  — name "GPU VRAM"、unit "%"、range bounded[0,100]（主維度為使用率 %，
//                   可正規化供長條 / 直方圖；已用/總量 bytes 置於實例值的 text 供顯示）
//   - "gpu.temp"  — name "GPU Temperature"、unit "°C"、range at_least(0)（溫度下界 0、
//                   上不設假界——溫度無自然上限，寧缺勿濫，需正規化的消費者自處理 nullopt）
//   三個指標共用同一組 GPU 列舉：實例 "gpu0" / "gpu1" / …(label "GPU 0" / "GPU 1" / …)。
//   各實例保留時序歷史（history_capacity>0），配合 E2-02 週期採集鋪成序列，供折線 /
//   sparkline 類元件直接鋪繪。無 GPU（如 null 期預設）時每個指標為 0 實例——誠實表達。
#ifndef DS_MODULES_E2_05_GPU_STATS_HPP
#define DS_MODULES_E2_05_GPU_STATS_HPP

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
// GpuStat：單一張 GPU 的一份讀值快照（平台中立值）
// ---------------------------------------------------------------------------
// 三組彼此獨立的讀值，各帶自己的 valid 旗標——真實硬體常有「使用率可讀但溫度感測缺」
// 之類情形，故每欄各自誠實表達，不因一欄缺讀而整張未知：
//   - usage      使用率**比率** [0,1]（提供者掛指標時再乘 100 成 %）；usage_valid==false = 無讀值。
//   - vram_used  已用 VRAM bytes；vram_total 總量 VRAM bytes；vram_valid==false = 無讀值。
//                （總量 0 時使用率百分比無從計算，提供者亦視為未知——不謊報 0。）
//   - temperature 溫度 °C；temp_valid==false = 無讀值。
// 刻意保持為 aggregate（可 `{...}` 初始化），另附靜態工廠與相等運算子增可讀性 / 可測性。
struct GpuStat {
    double usage = 0.0;             // 使用率比率 [0,1]
    bool usage_valid = false;       // false = 使用率無讀值

    std::uint64_t vram_used = 0;    // 已用 VRAM bytes
    std::uint64_t vram_total = 0;   // 總量 VRAM bytes
    bool vram_valid = false;        // false = VRAM 無讀值

    double temperature = 0.0;       // 溫度 °C
    bool temp_valid = false;        // false = 溫度無讀值

    // 明確的「整張 GPU 皆無讀值」（保守預設）。
    static GpuStat unknown() { return GpuStat{}; }

    // 便利工廠：一次給齊三組有效讀值。
    static GpuStat of(double usage_ratio, std::uint64_t used, std::uint64_t total,
                      double temp_c) {
        GpuStat g;
        g.usage = usage_ratio;      g.usage_valid = true;
        g.vram_used = used;         g.vram_total = total;  g.vram_valid = true;
        g.temperature = temp_c;     g.temp_valid = true;
        return g;
    }

    bool operator==(const GpuStat& o) const {
        return usage_valid == o.usage_valid && usage == o.usage &&
               vram_valid == o.vram_valid && vram_used == o.vram_used &&
               vram_total == o.vram_total && temp_valid == o.temp_valid &&
               temperature == o.temperature;
    }
    bool operator!=(const GpuStat& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// GpuStatSample：某一時刻**全部 GPU** 的讀值快照（每張一筆）
// ---------------------------------------------------------------------------
// gpus[i] = 第 i 張 GPU 的讀值。GPU 數 = gpus.size()，可在取樣間變動（外接 / 熱插拔 /
// 驅動上下線）——提供者據此動態增減實例。
struct GpuStatSample {
    std::vector<GpuStat> gpus;

    std::size_t gpu_count() const noexcept { return gpus.size(); }
    bool empty() const noexcept { return gpus.empty(); }

    bool operator==(const GpuStatSample& o) const { return gpus == o.gpus; }
    bool operator!=(const GpuStatSample& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// VRAM 使用率（自由函式，獨立可測；平台中立純算術）
// ---------------------------------------------------------------------------
// used / total 的使用率比率，夾到 [0,1]。total==0（無從判斷）→ 0（不謊報）；
// used>total（理論不該發生）→ 夾到 1.0。
double vram_ratio(std::uint64_t used, std::uint64_t total) noexcept;

// bytes → 人類可讀字串（如 "8.0 GiB" / "512 MiB" / "4096 B"），供 VRAM 實例值的 text 顯示。
std::string humanize_bytes(std::uint64_t bytes);

// ---------------------------------------------------------------------------
// GpuStatSource：GPU 讀值的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：每次 sample() 列舉所有 GPU，回一份 GpuStatSample。實作決定其來源
// （注入式假來源 / 真實後端）。sample() **非 const**：序列型來源每次取樣會推進內部游標，
// 故取樣具副作用。
class GpuStatSource {
public:
    virtual ~GpuStatSource() = default;

    // 取一份目前的全 GPU 讀值快照。無 GPU / 無讀值時回空 / 未知快照。
    virtual GpuStatSample sample() = 0;

protected:
    GpuStatSource() = default;
    GpuStatSource(const GpuStatSource&) = default;
    GpuStatSource& operator=(const GpuStatSource&) = default;
};

// ---------------------------------------------------------------------------
// NullGpuStatSource：相位 1 的 null / 假來源
// ---------------------------------------------------------------------------
// **不接任何真實 GPU API**。持一列注入的 GpuStatSample（可為單份「固定」或多份「序列」），
// 每次 sample() 回下一份；列盡則持續回最後一份（穩定，避免走出界）。空列（預設）回空快照
// ——Mac / null 期的誠實預設「無 GPU / 無讀值」。真實查詢留待後端相位——本類永不含平台呼叫。
class NullGpuStatSource : public GpuStatSource {
public:
    NullGpuStatSource() = default;

    // 固定來源：單份快照，每次 sample() 皆回它（列盡回最後一份 = 恆回同一份）。
    explicit NullGpuStatSource(GpuStatSample fixed) { sequence_.push_back(std::move(fixed)); }

    // 序列來源：多份快照（模擬時間推進下的變動），每次 sample() 回下一份。
    explicit NullGpuStatSource(std::vector<GpuStatSample> sequence)
        : sequence_(std::move(sequence)) {}

    // 設為固定單份快照（重置游標）。
    void set_fixed(GpuStatSample s) {
        sequence_.clear();
        sequence_.push_back(std::move(s));
        cursor_ = 0;
    }
    // 便利：以一列 GpuStat 設為固定單份快照。
    void set_gpus(std::vector<GpuStat> gpus) {
        GpuStatSample s;
        s.gpus = std::move(gpus);
        set_fixed(std::move(s));
    }
    // 注入 / 覆寫整條序列（重置游標到起點）。
    void set_sequence(std::vector<GpuStatSample> sequence) {
        sequence_ = std::move(sequence);
        cursor_ = 0;
    }
    // 追加一份快照到序列尾。
    void push_sample(GpuStatSample s) { sequence_.push_back(std::move(s)); }
    // 重置游標到序列起點。
    void reset() noexcept { cursor_ = 0; }
    // 回到「無 GPU / 無讀值」預設（清空序列）。
    void clear() noexcept {
        sequence_.clear();
        cursor_ = 0;
    }

    std::size_t size() const noexcept { return sequence_.size(); }
    bool empty() const noexcept { return sequence_.empty(); }

    // 回下一份快照；列盡回最後一份；空列回空快照。
    GpuStatSample sample() override;

private:
    std::vector<GpuStatSample> sequence_;
    std::size_t cursor_ = 0;
};

// ---------------------------------------------------------------------------
// GpuStatsProvider：把 GPU 使用率 / VRAM / 溫度掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上三個指標
// "gpu.usage" / "gpu.vram" / "gpu.temp"，各以每張 GPU 為一個可列舉實例（gpu0 / gpu1 / …）。
// 因 GPU 讀值會隨時間變動，本提供者建議以 **E2-02 的週期分級**採集：呼叫端把各 metric_id
// 與 sampling_tier() 登記到 SamplingScheduler，於排程器判定該採集時呼叫 sample() 重新讀
// 來源並更新各實例。消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，
// 完全不觸及本型別。
class GpuStatsProvider : public ds::metrics::MetricProvider {
public:
    static constexpr const char* kProviderId = "sysinfo.gpu";

    // 三個指標識別碼 / 顯示名。
    static constexpr const char* kUsageId = "gpu.usage";
    static constexpr const char* kVramId = "gpu.vram";
    static constexpr const char* kTempId = "gpu.temp";
    static constexpr const char* kUsageName = "GPU Usage";
    static constexpr const char* kVramName = "GPU VRAM";
    static constexpr const char* kTempName = "GPU Temperature";

    // 單位：使用率 / VRAM 皆為 %；溫度為 °C。
    static constexpr const char* kPercentUnit = "%";
    static constexpr const char* kTempUnit = "°C";

    // 使用率 / VRAM 比率 [0,1] → 百分比 % 的縮放。
    static constexpr double kPercentScale = 100.0;

    // 各實例歷史環的預設容量（配合 E2-02 週期採集鋪成序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：GPU 讀值變動但不若 CPU 需跟手，屬常規（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Normal;

    // 以一個 GPU 讀值來源建構。source 為 null 時，提供者仍會掛上三個指標，且皆以 0 實例
    // （無 GPU）呈現（保守而不崩、不謊報）。history 為各實例歷史環容量、tier 為建議採集分級。
    explicit GpuStatsProvider(std::shared_ptr<GpuStatSource> source,
                              std::size_t history = kDefaultHistory,
                              ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端對三個 metric_id add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 對註冊表掛上三個指標：取一份讀值、依 GPU 數建立各指標的每 GPU 實例、填初值，並保留
    // 指標參照供日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把新讀值寫入各實例（有效值推入歷史）。呼叫端在 E2-02 排程器判定本指標
    // 該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    // 若新取樣的 GPU 數多於既有實例，會**動態新增** GPU 實例（既有參照不失效）；
    // 若較少，多出的 GPU 實例三欄皆設為未知（不污染歷史）。
    void sample();

    // 目前的 GPU 實例數。register_metrics 前為 0。
    std::size_t gpu_count() const noexcept { return usage_insts_.size(); }

private:
    // 把一份快照寫入各指標的各實例。to_history 為真時有效值推入歷史（採集路徑），
    // 為假時只設值不動歷史。必要時動態擴增 GPU 實例。
    void apply(const GpuStatSample& sample, bool to_history);

    // 目前讀值快照：source_ 為 null 時視為空（無 GPU / 無讀值）。
    GpuStatSample current() { return source_ ? source_->sample() : GpuStatSample{}; }

    // 確保三個指標各有第 i 個 GPU 實例（不足則建立；既有參照不失效）。
    void ensure_gpu_instances(std::size_t count);

    // 把一個純數值（valid 決定是否未知 / 是否推歷史）寫入某實例；可附文字表述。
    void write_value(ds::metrics::InMemoryMetricInstance* inst, double number, bool valid,
                     bool to_history, std::string text = {});

    std::shared_ptr<GpuStatSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有，供 sample() 更新（與 registry 共享同一物件，故更新對消費者
    // 可見）。非擁有指標指向各 metric 內的實例（unique_ptr 持有，新增更多 GPU 實例時既有
    // 參照不失效）。三個指標共用同一 GPU 列舉，三組實例向量同步增長。
    std::shared_ptr<ds::metrics::InMemoryMetric> usage_metric_;
    std::shared_ptr<ds::metrics::InMemoryMetric> vram_metric_;
    std::shared_ptr<ds::metrics::InMemoryMetric> temp_metric_;
    std::vector<ds::metrics::InMemoryMetricInstance*> usage_insts_;
    std::vector<ds::metrics::InMemoryMetricInstance*> vram_insts_;
    std::vector<ds::metrics::InMemoryMetricInstance*> temp_insts_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_05_GPU_STATS_HPP
