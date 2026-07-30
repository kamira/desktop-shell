// E2-18 效能計數器（任意）— 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：以字串鍵註冊任意計數器、透過可注入來源讀值（Instant 原樣 / Rate 兩次取樣差分）、
// 以自訂 Metric / MetricInstance（支援動態新增 / 移除實例）掛上 E2-01 註冊表，並支援
// sample() 重新讀取更新（有效值推入歷史）。
// 無 `#ifdef`、無系統呼叫、無真實效能計數器 API（無 PDH / sysctl / mach）——換平台一行不動。
#include "perf_counter.hpp"

#include <algorithm>
#include <stdexcept>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// CounterMode 自由函式
// ---------------------------------------------------------------------------
const char* to_string(CounterMode mode) noexcept {
    switch (mode) {
        case CounterMode::Instant: return "instant";
        case CounterMode::Rate:    return "rate";
    }
    return "unknown";  // 不可達；防禦性
}

// ---------------------------------------------------------------------------
// NullPerfCounterSource
// ---------------------------------------------------------------------------
NullPerfCounterSource::Entry* NullPerfCounterSource::find(const std::string& key) {
    for (auto& e : entries_) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

const NullPerfCounterSource::Entry* NullPerfCounterSource::find(const std::string& key) const {
    for (const auto& e : entries_) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

void NullPerfCounterSource::set_value(const std::string& key, double value) {
    Entry* e = find(key);
    if (e) {
        e->seq = {value};
        e->cursor = 0;
        e->is_sequence = false;
        return;
    }
    entries_.push_back(Entry{key, {value}, 0, /*is_sequence=*/false});
}

void NullPerfCounterSource::set_sequence(const std::string& key, std::vector<double> sequence) {
    Entry* e = find(key);
    if (e) {
        e->seq = std::move(sequence);
        e->cursor = 0;
        e->is_sequence = true;
        return;
    }
    entries_.push_back(Entry{key, std::move(sequence), 0, /*is_sequence=*/true});
}

bool NullPerfCounterSource::remove(const std::string& key) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&key](const Entry& e) { return e.key == key; });
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
}

void NullPerfCounterSource::clear() {
    entries_.clear();
}

bool NullPerfCounterSource::has(const std::string& key) const {
    return find(key) != nullptr;
}

std::optional<double> NullPerfCounterSource::read(const std::string& key) {
    Entry* e = find(key);
    if (!e || e->seq.empty()) {
        return std::nullopt;  // 查無鍵 / 空序列 → 誠實無讀值
    }
    if (!e->is_sequence) {
        return e->seq.front();  // 固定值：原樣回，不推進
    }
    // 序列：回目前份並推進；列盡持續回最後一份（穩定，不走出界）。
    const std::size_t idx = std::min(e->cursor, e->seq.size() - 1);
    if (e->cursor < e->seq.size()) ++e->cursor;
    return e->seq[idx];
}

std::vector<std::string> NullPerfCounterSource::available_keys() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const auto& e : entries_) out.push_back(e.key);
    return out;
}

// ---------------------------------------------------------------------------
// PerfCounterInstance
// ---------------------------------------------------------------------------
PerfCounterInstance::PerfCounterInstance(std::string key, std::string label, CounterMode mode,
                                         std::size_t history_capacity)
    : key_(std::move(key)),
      label_(std::move(label)),
      mode_(mode),
      value_(ds::metrics::MetricValue::unknown()),
      history_(history_capacity) {}

void PerfCounterInstance::observe(std::optional<double> raw) {
    using ds::metrics::MetricValue;

    if (!raw.has_value()) {
        // 無讀值：設未知、不動歷史、不動 Rate 基準（下次有讀值再續差分）。
        value_ = MetricValue::unknown();
        return;
    }

    if (mode_ == CounterMode::Instant) {
        value_ = MetricValue::of(*raw);
        history_.push(*raw);  // 瞬時值直接入歷史
        return;
    }

    // Rate：累積計數器兩次取樣差分。
    if (!primed_) {
        prev_ = *raw;        // 設基準
        primed_ = true;
        value_ = MetricValue::unknown();  // 差分至少需兩份
        return;
    }
    double delta;
    if (*raw < *prev_) {
        delta = 0.0;  // 計數器重置（累積值理應單調）→ 保守 0，不謊報負值
    } else {
        delta = *raw - *prev_;
    }
    prev_ = *raw;  // 推進基準供下次差分
    value_ = MetricValue::of(delta);
    history_.push(delta);
}

void PerfCounterInstance::reset() {
    primed_ = false;
    prev_ = std::nullopt;
    value_ = ds::metrics::MetricValue::unknown();
}

// ---------------------------------------------------------------------------
// PerfCounterMetric
// ---------------------------------------------------------------------------
PerfCounterMetric::PerfCounterMetric(ds::metrics::MetricId id, std::string name,
                                     std::string unit, ds::metrics::MetricRange range)
    : id_(std::move(id)),
      name_(std::move(name)),
      unit_(std::move(unit)),
      range_(range) {}

PerfCounterInstance* PerfCounterMetric::add(std::string key, std::string label,
                                            CounterMode mode, std::size_t history_capacity) {
    if (PerfCounterInstance* existing = find(key)) {
        return existing;  // 已存在：不重複新增，回既有
    }
    instances_.push_back(std::make_unique<PerfCounterInstance>(
        std::move(key), std::move(label), mode, history_capacity));
    return instances_.back().get();
}

bool PerfCounterMetric::remove(const std::string& key) {
    auto it = std::find_if(instances_.begin(), instances_.end(),
                           [&key](const std::unique_ptr<PerfCounterInstance>& p) {
                               return p->instance_id() == key;
                           });
    if (it == instances_.end()) return false;
    instances_.erase(it);
    return true;
}

PerfCounterInstance* PerfCounterMetric::find(const std::string& key) {
    for (auto& p : instances_) {
        if (p->instance_id() == key) return p.get();
    }
    return nullptr;
}

PerfCounterInstance* PerfCounterMetric::mutable_instance(std::size_t i) {
    if (i >= instances_.size()) return nullptr;
    return instances_[i].get();
}

const ds::metrics::MetricInstance& PerfCounterMetric::instance(std::size_t i) const {
    if (i >= instances_.size()) {
        throw std::out_of_range("PerfCounterMetric::instance index out of range");
    }
    return *instances_[i];
}

// ---------------------------------------------------------------------------
// PerfCounterProvider
// ---------------------------------------------------------------------------
bool PerfCounterProvider::add_counter(const std::string& key, std::string label,
                                      CounterMode mode) {
    if (key.empty()) return false;  // 空鍵無效
    if (tracks_counter(key)) return false;  // 已註冊：保守拒絕，不重複

    if (!metric_) {
        // 尚未掛上：暫存到待掛佇列。
        pending_.push_back(CounterSpec{key, std::move(label), mode});
        return true;
    }
    // 已掛上：動態新增實例並立即讀一份初值（與 register 路徑一致）。
    PerfCounterInstance* inst = metric_->add(key, std::move(label), mode, history_);
    inst->observe(read_key(key));
    return true;
}

bool PerfCounterProvider::remove_counter(const std::string& key) {
    if (!metric_) {
        auto it = std::find_if(pending_.begin(), pending_.end(),
                               [&key](const CounterSpec& s) { return s.key == key; });
        if (it == pending_.end()) return false;
        pending_.erase(it);
        return true;
    }
    return metric_->remove(key);
}

bool PerfCounterProvider::tracks_counter(const std::string& key) const {
    if (metric_) {
        return const_cast<PerfCounterMetric&>(*metric_).find(key) != nullptr;
    }
    return std::any_of(pending_.begin(), pending_.end(),
                       [&key](const CounterSpec& s) { return s.key == key; });
}

std::size_t PerfCounterProvider::counter_count() const noexcept {
    return metric_ ? metric_->instance_count() : pending_.size();
}

std::vector<std::string> PerfCounterProvider::available_keys() const {
    return source_ ? source_->available_keys() : std::vector<std::string>{};
}

void PerfCounterProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 自訂 Metric（支援動態新增 / 移除實例）：任意異質計數器，無統一單位、無值域。
    metric_ = std::make_shared<PerfCounterMetric>(
        kMetricId, kMetricName, kUnit, ds::metrics::MetricRange::unbounded());

    // 把待掛計數器建成實例，並各讀一份初值（Instant 入歷史；Rate 設基準回未知）。
    for (auto& spec : pending_) {
        PerfCounterInstance* inst =
            metric_->add(spec.key, spec.label, spec.mode, history_);
        inst->observe(read_key(spec.key));
    }
    pending_.clear();

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(metric_);
}

void PerfCounterProvider::sample() {
    if (!metric_) return;  // 尚未 register_metrics：無指標可更新
    const std::size_t n = metric_->instance_count();
    for (std::size_t i = 0; i < n; ++i) {
        PerfCounterInstance* inst = metric_->mutable_instance(i);
        inst->observe(read_key(inst->instance_id()));
    }
}

}  // namespace ds::sysinfo
