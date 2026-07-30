// E2-22 登錄檔讀取 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：記憶體樹的讀取 / 子項列舉、值→MetricValue 對映、以 E2-01 記憶體內實作把
// 每個選定鍵建成一個實例、掛上註冊表。
// 無 `#ifdef`、無系統呼叫、無真實登錄後端——換平台一行不動。
#include "registry_read.hpp"

#include <set>
#include <string>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// NullRegistrySource（記憶體樹）
// ---------------------------------------------------------------------------
std::optional<RegistryValue> NullRegistrySource::read(const std::string& path) const {
    auto it = values_.find(path);
    if (it == values_.end()) return std::nullopt;  // 查無鍵：明確回報，不回假值
    return it->second;
}

std::vector<std::string> NullRegistrySource::enumerate(const std::string& path) const {
    // 直屬子項 = 所有以 (path + 分段符) 為前綴之已存值路徑，其緊接之下一段名稱。
    // path 為空字串時列舉根層（頂層段名）。以 std::set 去重 + 字典序（決定性）。
    const char sep = RegistrySource::kSeparator;
    std::string prefix = path;
    if (!prefix.empty()) prefix.push_back(sep);

    std::set<std::string> children;
    for (const auto& kv : values_) {
        const std::string& full = kv.first;
        if (full.size() <= prefix.size()) continue;
        if (full.compare(0, prefix.size(), prefix) != 0) continue;
        // 取前綴之後到下一分段符（或字串尾）的那一段。
        const std::size_t start = prefix.size();
        std::size_t end = full.find(sep, start);
        if (end == std::string::npos) end = full.size();
        if (end > start) children.insert(full.substr(start, end - start));
    }
    return std::vector<std::string>(children.begin(), children.end());
}

// ---------------------------------------------------------------------------
// registryValueToMetric：讀取結果 → E2-01 MetricValue
// ---------------------------------------------------------------------------
ds::metrics::MetricValue registryValueToMetric(const std::optional<RegistryValue>& v) {
    if (!v.has_value()) {
        return ds::metrics::MetricValue::unknown();  // 查無鍵 → 未知（valid==false）
    }
    switch (v->type()) {
        case RegistryType::Integer: {
            const std::int64_t n = v->as_integer().value();
            return ds::metrics::MetricValue::of(static_cast<double>(n), std::to_string(n));
        }
        case RegistryType::String: {
            // 文字值：數值維度不適用，僅承載文字（valid==true）。
            return ds::metrics::MetricValue::of(0.0, v->as_string().value());
        }
        case RegistryType::Binary: {
            const std::size_t n = v->as_binary().value().size();
            return ds::metrics::MetricValue::of(
                0.0, "<binary:" + std::to_string(n) + " bytes>");
        }
    }
    // 不可達（列舉已窮舉）；保守回未知。
    return ds::metrics::MetricValue::unknown();
}

// ---------------------------------------------------------------------------
// RegistryReaderProvider
// ---------------------------------------------------------------------------
void RegistryReaderProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // 登錄值為點查詢、無時序，故範圍無界、無單位、各實例 history_capacity=0。
    auto metric = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, /*unit=*/"", ds::metrics::MetricRange::unbounded());

    for (const auto& key : keys_) {
        // 每個選定鍵 = 一個可列舉實例：instance_id = label = 鍵路徑、無歷史。
        auto& inst = metric->add_instance(key, key, /*history_capacity=*/0);
        // source 為 null → 讀值視為查無鍵（未知）；否則讀取並對映。
        std::optional<RegistryValue> read = source_ ? source_->read(key) : std::nullopt;
        // 用 set_value（不推歷史）——與 capacity 0 一致。
        inst.set_value(registryValueToMetric(read));
    }

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(std::move(metric));
}

}  // namespace ds::sysinfo
