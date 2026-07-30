// E2-18 效能計數器（任意）— sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：提供**任意具名效能計數器**的泛用 sysinfo 提供者。讓外部以字串鍵註冊感興趣的
// 計數器（如 Windows PerfCounter 路徑 "\Processor(_Total)\% Processor Time"、macOS 統計
// 鍵、任意自訂鍵），提供者透過**可注入的來源**讀取每個計數器目前值，並以 **E2-01 的
// MetricProvider 介面**掛成一個指標——**每個計數器一個可列舉實例**。這是「新增指標 =
// 新增 MetricProvider、掛件一行不動」機制的又一個具體提供者，且刻意做成「任意鍵」的泛用
// 收集器：它**消費 E2-01 / E2-02 契約、不自造指標模型或排程器**。
//
// 兩種讀值模式（每個計數器可各自選）：
//   - **Instant（瞬時值）**：來源給的讀數即目前值（如「目前佇列長度」「目前溫度」）。
//   - **Rate（累積差分／速率）**：來源給的是**單調累積計數器**（如「累計封包數」）；
//     提供者以「兩次取樣差分」算速率 = currΔ（每次取樣間隔的增量）。首次取樣（尚無基準）
//     誠實回未知；計數器重置（curr < prev）保守回 0（不謊報負值 / 爆量）。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不接任何真實效能計數器 API**（無 PDH /
//     `\Processor(...)` / `sysctl` / `#ifdef` / win32 / cocoa）。真實後端（相位 2+）另實作
//     抽象來源 `PerfCounterSource`，提供者一行不動。
//   - 無 `#ifdef`、無系統呼叫、無平台分支——換平台一行不動（backend_guard 綠燈）。
//   - 誠實語意：查無鍵 / 無讀值 → 該實例 valid==false（未知），**不靜默**當成 0。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id    = "perf.counters"
//   - name  = "Performance Counters"
//   - unit  = ""（任意異質計數器，無統一單位——各計數器語意不同）
//   - range = unbounded（任意值域）
//   - **可列舉實例 = 各具名計數器**：每個註冊鍵一個 MetricInstance，
//     instance_id = 計數器鍵、label = 人類可讀名、value = 目前值（Instant）或速率（Rate）。
//     各實例保留時序歷史，配合 E2-02 週期採集鋪成序列。
//
// 動態性：計數器可在**執行期新增 / 移除**（`add_counter` / `remove_counter`），故本單元
// 用**自訂的 Metric / MetricInstance 實作**（而非 E2-01 的 InMemory* ——後者不支援移除
// 實例），同時仍嚴格實作 E2-01 的抽象契約，消費者一行不動。
#ifndef DS_MODULES_E2_18_PERF_COUNTER_HPP
#define DS_MODULES_E2_18_PERF_COUNTER_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 採集頻率分級（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// CounterMode：一個計數器的讀值模式
// ---------------------------------------------------------------------------
//   - Instant：來源讀數即目前值（原樣暴露）。
//   - Rate   ：來源讀數為**累積計數器**；提供者以兩次取樣差分算速率（每次取樣間隔的增量）。
enum class CounterMode {
    Instant,
    Rate,
};

// 診斷用穩定字串（"instant" / "rate"）。
const char* to_string(CounterMode mode) noexcept;

// ---------------------------------------------------------------------------
// PerfCounterSource：任意具名效能計數器的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 提供者只依賴此抽象：依字串鍵讀目前值、並可列舉目前可用的鍵。真實平台後端（相位 2+）
// 於此接 PDH / sysctl / 自訂統計；相位 1 只有注入式 NullPerfCounterSource。
//
// `read` **非 const**：序列型假來源每次讀取會推進內部游標（取樣具副作用），與 E2-03 的
// tick 來源同風格。
class PerfCounterSource {
public:
    virtual ~PerfCounterSource() = default;

    // 讀某鍵目前的原始讀數。nullopt = 無讀值（查無此鍵 / 讀取失敗）——誠實表達未知。
    virtual std::optional<double> read(const std::string& key) = 0;

    // 列舉目前可用的計數器鍵（決定性順序）。供外部發現有哪些鍵可註冊。
    virtual std::vector<std::string> available_keys() const = 0;

protected:
    PerfCounterSource() = default;
    PerfCounterSource(const PerfCounterSource&) = default;
    PerfCounterSource& operator=(const PerfCounterSource&) = default;
};

// ---------------------------------------------------------------------------
// NullPerfCounterSource：相位 1 的 null / 假來源
// ---------------------------------------------------------------------------
// **不接任何真實效能計數器 API**。以「鍵 → 值 / 序列」注入映射供測試與假感測器情境：
//   - set_value(key, v)     ：固定讀數（每次 read 回同一 v，不推進）。
//   - set_sequence(key, seq)：序列讀數（每次 read 回下一份；列盡持續回最後一份；空列回
//                             nullopt）——模擬累積計數器隨時間推進，供 Rate 差分測試。
// 未注入的鍵 → read 回 nullopt（誠實：查無鍵）。預設空來源（Mac / null 期誠實預設）。
class NullPerfCounterSource : public PerfCounterSource {
public:
    NullPerfCounterSource() = default;

    // 設定某鍵的固定讀數（覆寫既有；不推進游標）。
    void set_value(const std::string& key, double value);

    // 設定某鍵的序列讀數（覆寫既有；重置該鍵游標到起點）。
    void set_sequence(const std::string& key, std::vector<double> sequence);

    // 移除某鍵（此後 read 該鍵回 nullopt）。存在回 true。
    bool remove(const std::string& key);

    // 清空所有鍵（回到空來源預設語意）。
    void clear();

    // 是否有此鍵。
    bool has(const std::string& key) const;

    // 目前注入的鍵數。
    std::size_t size() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }

    // 讀某鍵：固定值原樣回；序列值回下一份並推進（列盡回最後一份）；空 / 查無 → nullopt。
    std::optional<double> read(const std::string& key) override;

    // 目前注入的鍵（注入順序，決定性）。
    std::vector<std::string> available_keys() const override;

private:
    struct Entry {
        std::string key;
        std::vector<double> seq;       // 讀數序列（固定值即長度 1）
        std::size_t cursor = 0;        // 序列游標
        bool is_sequence = false;      // true = 每次 read 推進游標
    };

    // 以有序 vector 保存（決定性列舉順序）；線性查找（計數器數少）。
    std::vector<Entry> entries_;

    Entry* find(const std::string& key);
    const Entry* find(const std::string& key) const;
};

// ---------------------------------------------------------------------------
// PerfCounterInstance：單一具名計數器的可讀實例（實作 E2-01 的 MetricInstance）
// ---------------------------------------------------------------------------
// 持有目前值 + 歷史環 + 讀值模式（Instant / Rate）。Rate 模式另保存「上一份累積值」供
// 差分。observe() 由提供者於每次採集時餵入一份原始讀數（或 nullopt = 無讀值）。
class PerfCounterInstance : public ds::metrics::MetricInstance {
public:
    PerfCounterInstance(std::string key, std::string label, CounterMode mode,
                        std::size_t history_capacity);

    // 餵入一份原始讀數並更新目前值 / 歷史。
    //   - raw == nullopt（無讀值）：設為未知（valid==false），**不**動歷史（不謊報 0、
    //     不污染序列）。Rate 模式亦不動基準（下次有讀值再續差分）。
    //   - Instant：目前值 = 原始讀數，推入歷史。
    //   - Rate   ：首次（尚無基準）→ 設基準、回未知（差分至少需兩份）；其後 = currΔ
    //              （curr < prev 視為計數器重置 → 保守 0），推入歷史。
    void observe(std::optional<double> raw);

    // 重置 Rate 差分基準（下次 observe 又回「需兩份」語意）；亦清目前值為未知。不動歷史。
    void reset();

    CounterMode mode() const noexcept { return mode_; }

    std::string instance_id() const override { return key_; }
    std::string label() const override { return label_; }
    ds::metrics::MetricValue value() const override { return value_; }
    const ds::metrics::MetricHistory& history() const override { return history_; }

private:
    std::string key_;
    std::string label_;
    CounterMode mode_;
    ds::metrics::MetricValue value_;
    ds::metrics::MetricHistory history_;
    std::optional<double> prev_;   // Rate：上一份累積值（基準）
    bool primed_ = false;          // Rate：是否已有基準
};

// ---------------------------------------------------------------------------
// PerfCounterMetric：一組具名計數器組成的指標（實作 E2-01 的 Metric）
// ---------------------------------------------------------------------------
// 支援**執行期動態新增 / 移除實例**（E2-01 的 InMemoryMetric 不支援移除，故自訂）。
// 實例以 unique_ptr 持有——新增更多實例時既有實例位置不失效；移除以鍵定位並抹除。
class PerfCounterMetric : public ds::metrics::Metric {
public:
    PerfCounterMetric(ds::metrics::MetricId id, std::string name, std::string unit,
                      ds::metrics::MetricRange range);

    // 新增一個計數器實例，回傳其指標。key 已存在則不新增、回既有實例指標。
    PerfCounterInstance* add(std::string key, std::string label, CounterMode mode,
                             std::size_t history_capacity);

    // 移除指定鍵的實例。存在並移除回 true；不存在回 false。
    bool remove(const std::string& key);

    // 依鍵尋找（可變）；找不到回 nullptr。供提供者更新用。
    PerfCounterInstance* find(const std::string& key);

    // 依索引取可變實例（列舉順序）；越界回 nullptr。供提供者逐一 observe。
    PerfCounterInstance* mutable_instance(std::size_t i);

    // -- E2-01 Metric 契約 --
    ds::metrics::MetricId id() const override { return id_; }
    std::string name() const override { return name_; }
    std::string unit() const override { return unit_; }
    ds::metrics::MetricRange range() const override { return range_; }
    std::size_t instance_count() const override { return instances_.size(); }
    const ds::metrics::MetricInstance& instance(std::size_t i) const override;

private:
    ds::metrics::MetricId id_;
    std::string name_;
    std::string unit_;
    ds::metrics::MetricRange range_;
    std::vector<std::unique_ptr<PerfCounterInstance>> instances_;  // 列舉順序 = 新增順序
};

// ---------------------------------------------------------------------------
// PerfCounterProvider：把任意具名效能計數器掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。外部以 add_counter(key, label, mode) 註冊感興趣的
// 計數器；register_metrics() 向註冊表掛上單一指標 "perf.counters"，其可列舉實例 = 各註冊
// 計數器。以 **E2-02 的週期分級** 採集：呼叫端把 metric_id 與 sampling_tier() 登記到
// SamplingScheduler，於排程器判定該採集時呼叫 sample() 重讀來源更新各實例。消費者（掛件）
// 只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class PerfCounterProvider : public ds::metrics::MetricProvider {
public:
    static constexpr const char* kMetricId = "perf.counters";
    static constexpr const char* kProviderId = "sysinfo.perf";
    static constexpr const char* kMetricName = "Performance Counters";
    static constexpr const char* kUnit = "";  // 任意異質計數器，無統一單位

    // 各實例歷史環的預設容量（配合 E2-02 週期採集鋪成序列）。
    static constexpr std::size_t kDefaultHistory = 64;
    // 建議採集分級：任意計數器多為常規頻率（可由建構子覆寫）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Normal;

    // 以一個計數器來源建構。source 為 null 時，提供者仍會掛上指標，且各計數器實例以
    // 「未知」（valid==false）呈現（保守而不崩、不謊報 0）。history 為各實例歷史環容量、
    // tier 為建議採集分級。
    explicit PerfCounterProvider(std::shared_ptr<PerfCounterSource> source,
                                 std::size_t history = kDefaultHistory,
                                 ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), history_(history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // -- 計數器註冊（可在 register_metrics 前後皆呼叫）------------------------

    // 註冊一個感興趣的具名計數器。key 為空或已註冊 → 回 false（保守，不重複）。
    // register_metrics 之後呼叫會**動態新增**實例到已掛上的指標，並立即讀一份初值。
    bool add_counter(const std::string& key, std::string label,
                     CounterMode mode = CounterMode::Instant);

    // 移除一個已註冊計數器（連同其實例）。存在並移除回 true；不存在回 false。
    // register_metrics 之後呼叫會**動態移除**已掛上指標的對應實例。
    bool remove_counter(const std::string& key);

    // 是否正在追蹤此計數器。
    bool tracks_counter(const std::string& key) const;

    // 目前追蹤的計數器數（= 掛上後的實例數）。
    std::size_t counter_count() const noexcept;

    // 列舉來源目前可用的計數器鍵（供外部發現）。source 為 null 回空。
    std::vector<std::string> available_keys() const;

    // 對註冊表掛上 "perf.counters" 指標：建各計數器實例、讀初值，並保留指標參照供日後
    // sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀來源、把各計數器新讀數餵入對應實例（Instant 原樣、Rate 差分；有效值推入
    // 歷史）。register_metrics 尚未呼叫（無指標）時為 no-op。
    void sample();

private:
    // 待掛計數器規格（register_metrics 前的暫存佇列）。
    struct CounterSpec {
        std::string key;
        std::string label;
        CounterMode mode;
    };

    // 讀某鍵目前讀數：source_ 為 null 時視為無讀值。
    std::optional<double> read_key(const std::string& key) {
        return source_ ? source_->read(key) : std::nullopt;
    }

    std::shared_ptr<PerfCounterSource> source_;
    std::size_t history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 前：待掛計數器暫存於此（metric_ 為 null）。
    std::vector<CounterSpec> pending_;

    // register_metrics 後持有（與 registry 共享同一物件，故更新對消費者可見）。
    std::shared_ptr<PerfCounterMetric> metric_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_18_PERF_COUNTER_HPP
