// E2-20 網路連通性 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：以注入式來源給連通性快照、以 E2-01 記憶體內實作把「整體 online」建成單一實例、
// 把「每探測目標」建成可列舉實例，把五個面向（online、reachable、latency、loss、dns）掛上
// 註冊表，並支援 sample() 重新讀取更新（有效值推入歷史）。無 `#ifdef`、無系統呼叫、無真實
// 網路探測（無 socket / ping / getaddrinfo / mach）——換平台一行不動。
#include "connectivity.hpp"

#include <string>
#include <utility>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// ConnectivitySample
// ---------------------------------------------------------------------------
const ConnectivityTarget* ConnectivitySample::find(const std::string& name) const {
    for (const auto& t : targets) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 純邏輯自由函式（獨立可測）
// ---------------------------------------------------------------------------
double loss_pct_from_counts(std::uint64_t sent, std::uint64_t received) noexcept {
    if (sent == 0) {
        return 0.0;  // 未送出任何探測，無從判斷遺失率
    }
    if (received >= sent) {
        return 0.0;  // 收 >= 送（含全收）→ 無遺失（received>sent 為保守夾住）
    }
    const std::uint64_t lost = sent - received;
    return static_cast<double>(lost) / static_cast<double>(sent) * 100.0;
}

OnlineVerdict online_from_targets(const ConnectivitySample& sample) noexcept {
    OnlineVerdict v;
    for (const auto& t : sample.targets) {
        if (!t.reachable_valid) continue;  // 只採計帶有效可達性讀值的目標
        v.valid = true;                    // 至少一個目標有讀值 → 判定有效
        if (t.reachable) {
            v.online = true;               // 任一可達即視為 online
            return v;
        }
    }
    return v;  // 無任一有效可達性讀值 → valid==false（不謊報）
}

// ---------------------------------------------------------------------------
// NullConnectivitySource
// ---------------------------------------------------------------------------
ConnectivitySample& NullConnectivitySource::ensure_fixed() {
    if (sequence_.size() != 1) {
        // 折成單元素固定快照：保留既有第一份（若有），否則新建無讀值份。
        ConnectivitySample base =
            sequence_.empty() ? ConnectivitySample{} : std::move(sequence_.front());
        sequence_.clear();
        sequence_.push_back(std::move(base));
    }
    cursor_ = 0;
    return sequence_.front();
}

void NullConnectivitySource::set_online(bool online) {
    ConnectivitySample& s = ensure_fixed();
    s.online = online;
    s.online_valid = true;
    s.valid = true;
}

void NullConnectivitySource::set_target(const std::string& name, bool reachable,
                                        double latency_ms, double loss_pct,
                                        bool dns_resolvable) {
    ConnectivitySample& s = ensure_fixed();
    s.valid = true;
    ConnectivityTarget t;
    t.name = name;
    t.reachable = reachable;
    t.reachable_valid = true;
    t.latency_ms = latency_ms;
    t.latency_valid = true;
    t.loss_pct = loss_pct;
    t.loss_valid = true;
    t.dns_resolvable = dns_resolvable;
    t.dns_valid = true;
    // 覆寫同名目標，否則追加。
    for (auto& existing : s.targets) {
        if (existing.name == name) { existing = std::move(t); return; }
    }
    s.targets.push_back(std::move(t));
}

ConnectivitySample NullConnectivitySource::sample() {
    if (sequence_.empty()) {
        return ConnectivitySample::unknown();  // 空列 → 無讀值
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx =
        cursor_ < sequence_.size() ? cursor_ : sequence_.size() - 1;
    if (cursor_ < sequence_.size()) ++cursor_;
    return sequence_[idx];
}

// ---------------------------------------------------------------------------
// ConnectivityProvider
// ---------------------------------------------------------------------------
namespace {

// 逐目標面向 → (id, name, unit, range)。與 TargetFacet 枚舉同序。
struct TargetFacetSpec {
    const char* id;
    const char* name;
    const char* unit;
    ds::metrics::MetricRange range;
};

ds::metrics::MetricRange bool_range() { return ds::metrics::MetricRange::bounded(0.0, 1.0); }

TargetFacetSpec target_facet_spec(int facet) {
    switch (facet) {
        case ConnectivityProvider::kReachable:
            return {ConnectivityProvider::kReachableMetricId, "Target Reachable",
                    ConnectivityProvider::kBoolUnit, bool_range()};
        case ConnectivityProvider::kLatency:
            return {ConnectivityProvider::kLatencyMetricId, "Network Latency",
                    ConnectivityProvider::kLatencyUnit,
                    ds::metrics::MetricRange::at_least(0.0)};
        case ConnectivityProvider::kLoss:
            return {ConnectivityProvider::kLossMetricId, "Packet Loss",
                    ConnectivityProvider::kLossUnit,
                    ds::metrics::MetricRange::bounded(0.0, 100.0)};
        case ConnectivityProvider::kDns:
        default:
            return {ConnectivityProvider::kDnsMetricId, "DNS Resolvable",
                    ConnectivityProvider::kBoolUnit, bool_range()};
    }
}

// 從一筆目標讀值取某逐目標面向的（數值, 是否有效）。
std::pair<double, bool> target_facet_value(const ConnectivityTarget& t, int facet) {
    switch (facet) {
        case ConnectivityProvider::kReachable:
            return {t.reachable ? 1.0 : 0.0, t.reachable_valid};
        case ConnectivityProvider::kLatency:
            return {t.latency_ms, t.latency_valid};
        case ConnectivityProvider::kLoss:
            return {t.loss_pct, t.loss_valid};
        case ConnectivityProvider::kDns:
            return {t.dns_resolvable ? 1.0 : 0.0, t.dns_valid};
        default:
            return {0.0, false};
    }
}

}  // namespace

std::vector<ds::metrics::MetricId> ConnectivityProvider::metric_ids() {
    return {kOnlineMetricId, kReachableMetricId, kLatencyMetricId, kLossMetricId,
            kDnsMetricId};
}

void ConnectivityProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    using ds::metrics::InMemoryMetric;

    // 整體 online：單一實例（bounded 0..1，可正規化）。
    online_metric_ = std::make_shared<InMemoryMetric>(kOnlineMetricId, "Network Online",
                                                      kBoolUnit, bool_range());
    online_inst_ = &online_metric_->add_instance(kInternetInstanceId, "Internet", history_);

    // 四個逐目標指標（沿用 E2-01 記憶體內實作，不自造模型）。
    for (int f = 0; f < kTargetFacetCount; ++f) {
        const TargetFacetSpec spec = target_facet_spec(f);
        metrics_[f] = std::make_shared<InMemoryMetric>(spec.id, spec.name, spec.unit,
                                                       spec.range);
    }

    // 以目前一份快照填初值，並依其目標建立每目標實例（初建亦把有效值推入歷史，與後續
    // 採集路徑一致）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表（順序：online、reachable、latency、loss、dns）。重複 id 由註冊表保守拒絕。
    registry.register_metric(online_metric_);
    for (int f = 0; f < kTargetFacetCount; ++f) {
        registry.register_metric(metrics_[f]);
    }
}

void ConnectivityProvider::sample() {
    if (!online_metric_) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

std::size_t ConnectivityProvider::ensure_target(const std::string& name) {
    auto it = target_index_.find(name);
    if (it != target_index_.end()) {
        return it->second;
    }
    const std::size_t idx = target_names_.size();
    target_names_.push_back(name);
    target_index_.emplace(name, idx);
    // 於全部四個逐目標指標新增同名實例（label = 目標名）。unique_ptr 持有，既有參照不失效。
    for (int f = 0; f < kTargetFacetCount; ++f) {
        insts_[f].push_back(&metrics_[f]->add_instance(name, name, history_));
    }
    return idx;
}

void ConnectivityProvider::write_value(ds::metrics::InMemoryMetricInstance* inst,
                                       double number, bool valid, bool to_history) {
    using ds::metrics::MetricValue;
    if (!valid) {
        // 無讀值：設為未知且**不**推入歷史（不污染序列），保守不謊報 0。
        inst->set_value(MetricValue::unknown());
        return;
    }
    const MetricValue v = MetricValue::of(number);
    if (to_history) {
        inst->update(v);  // 推入歷史（update 於 valid 值才推）
    } else {
        inst->set_value(v);
    }
}

void ConnectivityProvider::apply(const ConnectivitySample& snap, bool to_history) {
    if (!snap.valid) {
        // 整體無讀值：online 與全部既有目標實例設未知（不推歷史），不新增目標。
        write_value(online_inst_, 0.0, /*valid=*/false, to_history);
        for (int f = 0; f < kTargetFacetCount; ++f) {
            for (auto* inst : insts_[f]) {
                write_value(inst, 0.0, /*valid=*/false, to_history);
            }
        }
        return;
    }

    // 整體 online（bool → 1/0）；online_valid==false 時誠實設未知。
    write_value(online_inst_, snap.online ? 1.0 : 0.0, snap.online_valid, to_history);

    // 先確保本次出現的目標都有實例（動態新增），並記錄本次出現的索引集合。
    std::vector<bool> seen(target_names_.size(), false);
    for (const auto& t : snap.targets) {
        const std::size_t idx = ensure_target(t.name);
        if (idx >= seen.size()) seen.resize(idx + 1, false);
        seen[idx] = true;
        for (int f = 0; f < kTargetFacetCount; ++f) {
            const auto vv = target_facet_value(t, f);
            write_value(insts_[f][idx], vv.first, vv.second, to_history);
        }
    }

    // 本次未出現的既有目標（移除 / 無讀值）→ 全面向設未知（不縮減、誠實表達消失）。
    for (std::size_t idx = 0; idx < target_names_.size(); ++idx) {
        if (idx < seen.size() && seen[idx]) continue;
        for (int f = 0; f < kTargetFacetCount; ++f) {
            write_value(insts_[f][idx], 0.0, /*valid=*/false, to_history);
        }
    }
}

}  // namespace ds::sysinfo
