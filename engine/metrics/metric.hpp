// E2-01 統一指標介面 — 擴充點 1 的契約（engine 層 / 平台中立）
//
// 這是平台五個擴充點的**第一個**（見 docs/architecture.md §五個擴充點）。它決定
// 「包含但不限於任何指標」能否成立：消費者（掛件 / widget）只依賴本檔的介面，
// **不依賴任何具體感測器**。每加一個新指標（CPU、GPU、網路、任意來源）都是新增一個
// MetricProvider，掛件一行都不用改——這正是「平台」與「工具」的分野。
//
// 六個要素（見 docs/architecture.md 擴充點表第 1 列）：
//   名稱(name) / 值(value) / 單位(unit) / 範圍(range) / 歷史(history) / 可列舉實例(instances)
//
// 分層約束（engine 層）：
//   - **平台中立、純邏輯**：無 `#ifdef`、無系統呼叫、無真實後端。換平台一行不動。
//   - 相位 1（Mac / null 期）：只有介面 + 記憶體內實作，可完全單元測試
//     （塞入假指標，驗註冊 / 查詢 / 歷史環狀 / 多實例列舉）。
//   - 契約優先：API 最小且穩定。約 25 個 sysinfo 提供者 + 眾多 widget 消費此介面，
//     形狀錯了整批要改，故寧缺勿濫——只放「名稱/值/單位/範圍/歷史/實例」六要素所需。
//
// 契約分兩層：
//   - **抽象介面**（Metric / MetricInstance / MetricProvider）：穩定契約，消費者只依賴它。
//   - **記憶體內實作**（InMemoryMetric / InMemoryMetricInstance）：相位 1 的具體提供，
//     供假感測器與測試使用；真實後端上線後由後端自行實作介面，介面不變。
#ifndef DS_ENGINE_E2_01_METRIC_HPP
#define DS_ENGINE_E2_01_METRIC_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ds::metrics {

// 指標的穩定識別碼（如 "cpu.usage"、"net.rx.bytes"）。跨平台一致，不隨後端而變。
// 命名慣例：<子系統>.<指標>[.<面向>]，與能力矩陣（E1-21）同風格。
using MetricId = std::string;

// ---------------------------------------------------------------------------
// 值(value)：數值 + 可選文字（多型值的最小形）
// ---------------------------------------------------------------------------
// 主維度是 `number`（供繪圖 / 正規化 / 歷史）；`text` 為可選的顯示 / 列舉表述
// （如 "Charging"、"82°C"），讓非純數值指標也能表達，而不必犧牲數值維度。
// `valid == false` 表示「目前無讀值」（尚未取樣 / 感測失敗），消費者據此顯示為未知，
// 而非把 0 誤當成真實讀值。
//
// 刻意保持為 aggregate（可 `{n, text, valid}` 初始化），另附靜態工廠增可讀性。
struct MetricValue {
    double number = 0.0;                  // 主數值維度
    std::optional<std::string> text;      // 可選文字 / 列舉表述
    bool valid = false;                   // false = 無讀值（未知），預設保守

    // 有效數值。
    static MetricValue of(double n) { return MetricValue{n, std::nullopt, true}; }
    // 有效數值 + 文字表述。
    static MetricValue of(double n, std::string t) { return MetricValue{n, std::move(t), true}; }
    // 明確的「無讀值」。
    static MetricValue unknown() { return MetricValue{}; }

    bool operator==(const MetricValue& o) const {
        return valid == o.valid && number == o.number && text == o.text;
    }
    bool operator!=(const MetricValue& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// 範圍(range)：min / max，皆可選（無界）
// ---------------------------------------------------------------------------
// 缺 min = 下無界；缺 max = 上無界。兩者皆有才「有界」，此時可正規化到 [0,1]，
// 讓長條 / 直方圖類元件在**不認識任何具體感測器**的前提下把任意指標畫成比例。
//
// 保持為 aggregate（`{min, max}`），附靜態工廠與純查詢方法。
struct MetricRange {
    std::optional<double> min;   // 下界；nullopt = 下無界
    std::optional<double> max;   // 上界；nullopt = 上無界

    static MetricRange unbounded() { return MetricRange{std::nullopt, std::nullopt}; }
    static MetricRange bounded(double lo, double hi) { return MetricRange{lo, hi}; }
    static MetricRange at_least(double lo) { return MetricRange{lo, std::nullopt}; }
    static MetricRange at_most(double hi) { return MetricRange{std::nullopt, hi}; }

    bool has_min() const noexcept { return min.has_value(); }
    bool has_max() const noexcept { return max.has_value(); }
    bool is_bounded() const noexcept { return has_min() && has_max(); }

    // 夾到存在的界內（缺哪界就不夾哪界）。
    double clamp(double v) const noexcept;

    // 正規化到 [0,1]。需有界且 max > min，否則回 nullopt（消費者據此改用其他呈現）。
    // 超界值會被夾到 [0,1]。
    std::optional<double> normalized(double v) const noexcept;
};

// ---------------------------------------------------------------------------
// 歷史(history)：近期值的環狀緩衝
// ---------------------------------------------------------------------------
// 固定容量的環狀緩衝：滿了之後 push 覆蓋最舊值。索引語意固定為
// **0 = 最舊 … size()-1 = 最新**，供折線 / sparkline 類元件直接鋪繪。
class MetricHistory {
public:
    // capacity 為保留的近期值數。capacity == 0 為合法（不保留歷史）：push 為 no-op、
    // size 恆為 0。
    explicit MetricHistory(std::size_t capacity);

    // 推入一個新值。滿了則覆蓋最舊。capacity==0 時為 no-op。
    void push(double v);

    std::size_t size() const noexcept { return count_; }
    std::size_t capacity() const noexcept { return capacity_; }
    bool empty() const noexcept { return count_ == 0; }
    bool full() const noexcept { return count_ == capacity_ && capacity_ != 0; }

    // 依時序取值：i==0 為最舊、i==size()-1 為最新。越界擲 std::out_of_range。
    double at(std::size_t i) const;

    // 最新值。empty() 時擲 std::out_of_range。
    double latest() const;

    // 依時序（最舊→最新）複製為 vector。
    std::vector<double> to_vector() const;

    // 清空（capacity 不變）。
    void clear() noexcept;

private:
    std::vector<double> buf_;   // 實體環，長度 == capacity_
    std::size_t capacity_;
    std::size_t head_ = 0;      // 下一次 push 的寫入位置
    std::size_t count_ = 0;     // 目前有效值數（≤ capacity_）
};

// ---------------------------------------------------------------------------
// MetricInstance：指標的單一可讀實例（如某一核心、某一顆磁碟）
// ---------------------------------------------------------------------------
// 單值指標恰有一個實例；多實例指標（每核心 CPU / 每顆磁碟）列舉多個。
// 掛件通常綁定到「一個實例」讀其 value + history。此為**抽象契約**，消費者只依賴它。
class MetricInstance {
public:
    virtual ~MetricInstance() = default;

    // 於所屬指標內穩定的實例識別碼（如 "cpu0"、"/dev/disk0"）。
    // 單值指標可用 ""（見 Metric::kSingleInstanceId）。
    virtual std::string instance_id() const = 0;

    // 人類可讀標籤（如 "Core 0"）。
    virtual std::string label() const = 0;

    // 目前讀值（valid==false 表未知）。
    virtual MetricValue value() const = 0;

    // 近期歷史（環狀）。
    virtual const MetricHistory& history() const = 0;

protected:
    MetricInstance() = default;
    MetricInstance(const MetricInstance&) = default;
    MetricInstance& operator=(const MetricInstance&) = default;
};

// ---------------------------------------------------------------------------
// Metric：一種指標的身分 + 單位 + 範圍 + 可列舉實例
// ---------------------------------------------------------------------------
// 這是消費者面對的核心契約。消費者透過 id 尋得 Metric，讀 name/unit/range，
// 列舉 instances 讀各自的 value/history——全程不觸及任何具體感測器型別。
class Metric {
public:
    // 單值指標的慣例實例識別碼。
    static constexpr const char* kSingleInstanceId = "";

    virtual ~Metric() = default;

    // 穩定識別碼（如 "cpu.usage"）。跨平台、跨後端一致。
    virtual MetricId id() const = 0;

    // 人類可讀顯示名（如 "CPU Usage"）。
    virtual std::string name() const = 0;

    // 單位字串（如 "%"、"MB/s"、"°C"；無單位用 ""）。
    virtual std::string unit() const = 0;

    // 值域範圍（可無界）。多實例通常共用同一範圍。
    virtual MetricRange range() const = 0;

    // 實例數（≥ 0；正常提供者 ≥ 1）。
    virtual std::size_t instance_count() const = 0;

    // 第 i 個實例（列舉順序）。越界擲 std::out_of_range。
    virtual const MetricInstance& instance(std::size_t i) const = 0;

    // --- 以下為介面內建的便利查詢，非虛擬，統一實作於介面上 ---

    // 是否單一實例。
    bool is_single() const { return instance_count() == 1; }

    // 單值指標的唯一實例。instance_count()!=1 時擲 std::out_of_range。
    const MetricInstance& single() const;

    // 依實例識別碼尋找；找不到回 nullptr。回傳指標於本物件存活期間有效。
    const MetricInstance* find_instance(const std::string& instance_id) const;

protected:
    Metric() = default;
    Metric(const Metric&) = default;
    Metric& operator=(const Metric&) = default;
};

// 前置宣告：提供者以它掛上指標。
class MetricRegistry;

// ---------------------------------------------------------------------------
// MetricProvider：把一個或多個指標「掛上」註冊表的提供者
// ---------------------------------------------------------------------------
// 每個感測器（CPU、GPU、網路、任意來源）實作它。register_metrics() 內對 registry
// 註冊自己提供的指標。這是「新增指標 = 新增提供者、消費者不動」的機制端。
class MetricProvider {
public:
    virtual ~MetricProvider() = default;

    // 提供者穩定識別碼（如 "sysinfo.cpu"）。供診斷 / 去重 / 溯源。
    virtual std::string provider_id() const = 0;

    // 對註冊表掛上本提供者的指標。可註冊零個或多個。
    virtual void register_metrics(MetricRegistry& registry) = 0;

protected:
    MetricProvider() = default;
    MetricProvider(const MetricProvider&) = default;
    MetricProvider& operator=(const MetricProvider&) = default;
};

// ---------------------------------------------------------------------------
// MetricRegistry：註冊 / 查詢 / 列舉指標
// ---------------------------------------------------------------------------
// 消費者的唯一入口：只依賴本類與 Metric 介面，永不依賴任何具體感測器。
// 列舉順序為**註冊順序**（決定性，供測試與穩定 UI 排列）。
class MetricRegistry {
public:
    MetricRegistry() = default;

    // 不可複製（持有 shared_ptr 集合，複製語意易誤用）；可移動。
    MetricRegistry(const MetricRegistry&) = delete;
    MetricRegistry& operator=(const MetricRegistry&) = delete;
    MetricRegistry(MetricRegistry&&) = default;
    MetricRegistry& operator=(MetricRegistry&&) = default;

    // 註冊一個指標。成功回 true。
    // 失敗（回 false 且不改動狀態）：指標為 null，或 id 為空，或 id 已存在
    // （保守：重複 id 視為錯誤，不覆寫既有——兩個感測器爭同一 id 是 bug，不該靜默勝出）。
    bool register_metric(std::shared_ptr<Metric> metric);

    // 移除指定 id 的指標。移除成功回 true；不存在回 false。
    bool unregister(const MetricId& id);

    // 是否已註冊該 id。
    bool contains(const MetricId& id) const;

    // 取得指標；不存在回 nullptr。回傳 shared_ptr 讓消費者可延長其壽命。
    std::shared_ptr<Metric> get(const MetricId& id) const;

    // 已註冊指標數。
    std::size_t size() const noexcept { return metrics_.size(); }
    bool empty() const noexcept { return metrics_.empty(); }

    // 全部指標（註冊順序）。
    const std::vector<std::shared_ptr<Metric>>& all() const noexcept { return metrics_; }

    // 全部 id（註冊順序）。
    std::vector<MetricId> ids() const;

    // 讓提供者掛上其指標：等同呼叫 provider.register_metrics(*this)。
    // 回傳此次因而**成功新增**的指標數。
    std::size_t add_provider(MetricProvider& provider);

private:
    std::vector<std::shared_ptr<Metric>> metrics_;          // 註冊順序
    std::unordered_map<MetricId, std::size_t> index_;       // id -> metrics_ 索引
};

// ===========================================================================
// 記憶體內實作（相位 1 具體提供 / 測試用）
// ===========================================================================

// 記憶體內的單一實例：持有目前值與歷史環，供假感測器 / 測試推入資料。
class InMemoryMetricInstance : public MetricInstance {
public:
    InMemoryMetricInstance(std::string instance_id, std::string label,
                           std::size_t history_capacity);

    // 設定目前值**並**把數值推入歷史（valid 值才推）。感測器每次取樣呼叫此。
    void update(const MetricValue& v);

    // 便利：以純數值更新（等同 update(MetricValue::of(number))）。
    void push(double number);

    // 只設目前值，不動歷史（如標記為未知而不污染歷史序列）。
    void set_value(const MetricValue& v);

    std::string instance_id() const override { return instance_id_; }
    std::string label() const override { return label_; }
    MetricValue value() const override { return value_; }
    const MetricHistory& history() const override { return history_; }

private:
    std::string instance_id_;
    std::string label_;
    MetricValue value_;
    MetricHistory history_;
};

// 記憶體內的指標：持有身分 / 單位 / 範圍 + 一組實例。
// 用 add_instance() 建構實例並取得參照以推值；實例以 unique_ptr 持有，
// 故新增更多實例時既有參照不失效。
class InMemoryMetric : public Metric {
public:
    InMemoryMetric(MetricId id, std::string name, std::string unit, MetricRange range);

    // 新增一個實例，回傳其參照供推值。history_capacity 為該實例歷史環容量。
    InMemoryMetricInstance& add_instance(std::string instance_id, std::string label,
                                         std::size_t history_capacity);

    MetricId id() const override { return id_; }
    std::string name() const override { return name_; }
    std::string unit() const override { return unit_; }
    MetricRange range() const override { return range_; }
    std::size_t instance_count() const override { return instances_.size(); }
    const MetricInstance& instance(std::size_t i) const override;

private:
    MetricId id_;
    std::string name_;
    std::string unit_;
    MetricRange range_;
    std::vector<std::unique_ptr<InMemoryMetricInstance>> instances_;
};

}  // namespace ds::metrics

#endif  // DS_ENGINE_E2_01_METRIC_HPP
