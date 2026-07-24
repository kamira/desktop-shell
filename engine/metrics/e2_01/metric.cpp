// E2-01 統一指標介面 — 實作（engine 層 / 平台中立）
//
// 純邏輯：環狀歷史、範圍正規化、註冊表增刪查、記憶體內實作。
// 無 `#ifdef`、無系統呼叫、無真實後端——換平台一行不動。
#include "metric.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace ds::metrics {

// ---------------------------------------------------------------------------
// MetricRange
// ---------------------------------------------------------------------------
double MetricRange::clamp(double v) const noexcept {
    if (min.has_value() && v < *min) v = *min;
    if (max.has_value() && v > *max) v = *max;
    return v;
}

std::optional<double> MetricRange::normalized(double v) const noexcept {
    if (!is_bounded()) return std::nullopt;
    const double lo = *min;
    const double hi = *max;
    if (!(hi > lo)) return std::nullopt;  // 退化 / 反向區間無法正規化
    double t = (v - lo) / (hi - lo);
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return t;
}

// ---------------------------------------------------------------------------
// MetricHistory（環狀緩衝）
// ---------------------------------------------------------------------------
MetricHistory::MetricHistory(std::size_t capacity)
    : buf_(capacity), capacity_(capacity) {}

void MetricHistory::push(double v) {
    if (capacity_ == 0) return;  // 不保留歷史
    buf_[head_] = v;
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_) ++count_;
    // 滿了之後 head_ 前進即覆蓋最舊值——環狀語意。
}

double MetricHistory::at(std::size_t i) const {
    if (i >= count_) throw std::out_of_range("MetricHistory::at index out of range");
    // 最舊值的實體位置：head_ 往回 count_ 格。
    const std::size_t start = (head_ + capacity_ - count_) % capacity_;
    return buf_[(start + i) % capacity_];
}

double MetricHistory::latest() const {
    if (count_ == 0) throw std::out_of_range("MetricHistory::latest on empty history");
    return at(count_ - 1);
}

std::vector<double> MetricHistory::to_vector() const {
    std::vector<double> out;
    out.reserve(count_);
    for (std::size_t i = 0; i < count_; ++i) out.push_back(at(i));
    return out;
}

void MetricHistory::clear() noexcept {
    head_ = 0;
    count_ = 0;
}

// ---------------------------------------------------------------------------
// Metric（介面內建便利查詢）
// ---------------------------------------------------------------------------
const MetricInstance& Metric::single() const {
    if (instance_count() != 1) {
        throw std::out_of_range("Metric::single requires exactly one instance");
    }
    return instance(0);
}

const MetricInstance* Metric::find_instance(const std::string& instance_id) const {
    const std::size_t n = instance_count();
    for (std::size_t i = 0; i < n; ++i) {
        const MetricInstance& inst = instance(i);
        if (inst.instance_id() == instance_id) return &inst;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// MetricRegistry
// ---------------------------------------------------------------------------
bool MetricRegistry::register_metric(std::shared_ptr<Metric> metric) {
    if (!metric) return false;
    const MetricId id = metric->id();
    if (id.empty()) return false;
    if (index_.find(id) != index_.end()) return false;  // 重複 id：保守拒絕，不覆寫
    index_.emplace(id, metrics_.size());
    metrics_.push_back(std::move(metric));
    return true;
}

bool MetricRegistry::unregister(const MetricId& id) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;
    const std::size_t pos = it->second;
    metrics_.erase(metrics_.begin() + static_cast<std::ptrdiff_t>(pos));
    index_.erase(it);
    // 移除後，pos 之後的索引全部前移一格——重建受影響區段的索引。
    for (auto& kv : index_) {
        if (kv.second > pos) --kv.second;
    }
    return true;
}

bool MetricRegistry::contains(const MetricId& id) const {
    return index_.find(id) != index_.end();
}

std::shared_ptr<Metric> MetricRegistry::get(const MetricId& id) const {
    auto it = index_.find(id);
    if (it == index_.end()) return nullptr;
    return metrics_[it->second];
}

std::vector<MetricId> MetricRegistry::ids() const {
    std::vector<MetricId> out;
    out.reserve(metrics_.size());
    for (const auto& m : metrics_) out.push_back(m->id());
    return out;
}

std::size_t MetricRegistry::add_provider(MetricProvider& provider) {
    const std::size_t before = metrics_.size();
    provider.register_metrics(*this);
    return metrics_.size() - before;
}

// ---------------------------------------------------------------------------
// InMemoryMetricInstance
// ---------------------------------------------------------------------------
InMemoryMetricInstance::InMemoryMetricInstance(std::string instance_id, std::string label,
                                               std::size_t history_capacity)
    : instance_id_(std::move(instance_id)),
      label_(std::move(label)),
      value_(MetricValue::unknown()),
      history_(history_capacity) {}

void InMemoryMetricInstance::update(const MetricValue& v) {
    value_ = v;
    if (v.valid) history_.push(v.number);  // 只把有效讀值納入歷史
}

void InMemoryMetricInstance::push(double number) {
    update(MetricValue::of(number));
}

void InMemoryMetricInstance::set_value(const MetricValue& v) {
    value_ = v;  // 不動歷史
}

// ---------------------------------------------------------------------------
// InMemoryMetric
// ---------------------------------------------------------------------------
InMemoryMetric::InMemoryMetric(MetricId id, std::string name, std::string unit, MetricRange range)
    : id_(std::move(id)),
      name_(std::move(name)),
      unit_(std::move(unit)),
      range_(range) {}

InMemoryMetricInstance& InMemoryMetric::add_instance(std::string instance_id, std::string label,
                                                     std::size_t history_capacity) {
    instances_.push_back(std::make_unique<InMemoryMetricInstance>(
        std::move(instance_id), std::move(label), history_capacity));
    return *instances_.back();
}

const MetricInstance& InMemoryMetric::instance(std::size_t i) const {
    if (i >= instances_.size()) {
        throw std::out_of_range("InMemoryMetric::instance index out of range");
    }
    return *instances_[i];
}

}  // namespace ds::metrics
