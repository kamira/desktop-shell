// E2-08 網路流量 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：兩次取樣差分算速率（或來源直接給流量）、以 E2-01 記憶體內實作把每網路介面
// 建成可列舉實例、把六個面向（rx/tx 速率、rx/tx 累積位元組、rx/tx 封包數）掛上註冊表，
// 並支援 sample() 重新讀取更新（有效值推入歷史）。無 `#ifdef`、無系統呼叫、無真實網路
// API（無 getifaddrs / /proc/net/dev / mach）——換平台一行不動。
#include "network_traffic.hpp"

#include <string>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// NetTrafficSample
// ---------------------------------------------------------------------------
const NetTrafficSample::Iface* NetTrafficSample::find(const std::string& name) const {
    for (const auto& i : interfaces) {
        if (i.name == name) return &i;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 差分演算法（自由函式，獨立可測）
// ---------------------------------------------------------------------------
double rate_from_delta(std::uint64_t prev, std::uint64_t curr, double dt_seconds) noexcept {
    if (dt_seconds <= 0.0) {
        return 0.0;  // 無經過時間，無從判斷
    }
    if (curr < prev) {
        return 0.0;  // 計數器重置（累積值理應單調遞增）→ 保守回 0
    }
    const std::uint64_t delta = curr - prev;
    return static_cast<double>(delta) / dt_seconds;
}

NetTrafficSample traffic_from_snapshot(const NetCountersSample& curr) {
    NetTrafficSample out;
    out.valid = true;  // 有一份讀值：累積計數直接可讀
    out.interfaces.reserve(curr.interfaces.size());
    for (const auto& e : curr.interfaces) {
        NetTrafficSample::Iface f;
        f.name = e.name;
        // 速率尚無前一份可差分 → 0 且 rate_valid==false（誠實：單一時刻無速率）。
        f.rx_rate = 0.0;
        f.tx_rate = 0.0;
        f.rate_valid = false;
        f.rx_bytes = e.counters.rx_bytes;
        f.tx_bytes = e.counters.tx_bytes;
        f.rx_packets = e.counters.rx_packets;
        f.tx_packets = e.counters.tx_packets;
        out.interfaces.push_back(std::move(f));
    }
    return out;
}

NetTrafficSample traffic_from_delta(const NetCountersSample& prev,
                                    const NetCountersSample& curr) {
    NetTrafficSample out;
    out.valid = true;  // 已有兩份取樣
    const double dt = curr.timestamp - prev.timestamp;
    out.interfaces.reserve(curr.interfaces.size());
    for (const auto& e : curr.interfaces) {
        NetTrafficSample::Iface f;
        f.name = e.name;
        // 累積計數直接取自 curr（不需差分）。
        f.rx_bytes = e.counters.rx_bytes;
        f.tx_bytes = e.counters.tx_bytes;
        f.rx_packets = e.counters.rx_packets;
        f.tx_packets = e.counters.tx_packets;

        // 於 prev 按名尋對應介面以差分速率。
        const NetInterfaceCounterEntry* p = nullptr;
        for (const auto& pe : prev.interfaces) {
            if (pe.name == e.name) { p = &pe; break; }
        }
        if (p != nullptr && dt > 0.0) {
            f.rx_rate = rate_from_delta(p->counters.rx_bytes, e.counters.rx_bytes, dt);
            f.tx_rate = rate_from_delta(p->counters.tx_bytes, e.counters.tx_bytes, dt);
            f.rate_valid = true;
        } else {
            // 新上線介面（prev 無對應）或無經過時間（dt<=0）→ 只有累積、暫無速率。
            f.rx_rate = 0.0;
            f.tx_rate = 0.0;
            f.rate_valid = false;
        }
        out.interfaces.push_back(std::move(f));
    }
    return out;
}

// ---------------------------------------------------------------------------
// NullNetworkStatSource
// ---------------------------------------------------------------------------
void NullNetworkStatSource::set_interface(const std::string& name, double rx_rate,
                                          double tx_rate, std::uint64_t rx_bytes,
                                          std::uint64_t tx_bytes, std::uint64_t rx_packets,
                                          std::uint64_t tx_packets) {
    fixed_.valid = true;
    NetTrafficSample::Iface f;
    f.name = name;
    f.rx_rate = rx_rate;
    f.tx_rate = tx_rate;
    f.rate_valid = true;
    f.rx_bytes = rx_bytes;
    f.tx_bytes = tx_bytes;
    f.rx_packets = rx_packets;
    f.tx_packets = tx_packets;
    // 覆寫同名介面，否則追加。
    for (auto& existing : fixed_.interfaces) {
        if (existing.name == name) { existing = std::move(f); return; }
    }
    fixed_.interfaces.push_back(std::move(f));
}

// ---------------------------------------------------------------------------
// NullNetCounterSource
// ---------------------------------------------------------------------------
NetCountersSample NullNetCounterSource::read() {
    if (sequence_.empty()) {
        return NetCountersSample{};  // 空列 → 空快照
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx =
        cursor_ < sequence_.size() ? cursor_ : sequence_.size() - 1;
    if (cursor_ < sequence_.size()) ++cursor_;
    return sequence_[idx];
}

// ---------------------------------------------------------------------------
// DifferencingNetworkStatSource
// ---------------------------------------------------------------------------
NetTrafficSample DifferencingNetworkStatSource::sample() {
    if (!counters_) {
        return NetTrafficSample::unknown();  // 無計數來源 → 無讀值
    }
    NetCountersSample curr = counters_->read();
    if (!primed_) {
        // 首次取樣：只有一份，速率尚無可差分的基準 → 速率未知（累積仍有效）。保存為基準。
        NetTrafficSample out = traffic_from_snapshot(curr);
        prev_ = std::move(curr);
        primed_ = true;
        return out;
    }
    NetTrafficSample traffic = traffic_from_delta(prev_, curr);
    prev_ = std::move(curr);  // 推進基準供下次差分
    return traffic;
}

// ---------------------------------------------------------------------------
// NetworkTrafficProvider
// ---------------------------------------------------------------------------
namespace {

// 面向 → (id, name, unit)。與 Facet 枚舉同序。
struct FacetSpec {
    const char* id;
    const char* name;
    const char* unit;
};

const FacetSpec kFacetSpecs[NetworkTrafficProvider::kFacetCount] = {
    {NetworkTrafficProvider::kRxRateMetricId, "Network RX Rate",
     NetworkTrafficProvider::kRateUnit},
    {NetworkTrafficProvider::kTxRateMetricId, "Network TX Rate",
     NetworkTrafficProvider::kRateUnit},
    {NetworkTrafficProvider::kRxBytesMetricId, "Network RX Bytes",
     NetworkTrafficProvider::kBytesUnit},
    {NetworkTrafficProvider::kTxBytesMetricId, "Network TX Bytes",
     NetworkTrafficProvider::kBytesUnit},
    {NetworkTrafficProvider::kRxPacketsMetricId, "Network RX Packets",
     NetworkTrafficProvider::kPacketsUnit},
    {NetworkTrafficProvider::kTxPacketsMetricId, "Network TX Packets",
     NetworkTrafficProvider::kPacketsUnit},
};

// 從一筆介面流量取某面向的（數值, 是否有效）。
std::pair<double, bool> facet_value(const NetTrafficSample::Iface& f, int facet) {
    switch (facet) {
        case NetworkTrafficProvider::kRxRate:
            return {f.rx_rate, f.rate_valid};
        case NetworkTrafficProvider::kTxRate:
            return {f.tx_rate, f.rate_valid};
        case NetworkTrafficProvider::kRxBytes:
            return {static_cast<double>(f.rx_bytes), true};
        case NetworkTrafficProvider::kTxBytes:
            return {static_cast<double>(f.tx_bytes), true};
        case NetworkTrafficProvider::kRxPackets:
            return {static_cast<double>(f.rx_packets), true};
        case NetworkTrafficProvider::kTxPackets:
            return {static_cast<double>(f.tx_packets), true};
        default:
            return {0.0, false};
    }
}

}  // namespace

std::vector<ds::metrics::MetricId> NetworkTrafficProvider::metric_ids() {
    std::vector<ds::metrics::MetricId> ids;
    ids.reserve(kFacetCount);
    for (int f = 0; f < kFacetCount; ++f) {
        ids.emplace_back(kFacetSpecs[f].id);
    }
    return ids;
}

void NetworkTrafficProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。六個面向指標，值域皆 at_least(0)
    // （速率 / 累積位元組 / 封包數皆非負、上無界）。
    for (int f = 0; f < kFacetCount; ++f) {
        metrics_[f] = std::make_shared<ds::metrics::InMemoryMetric>(
            kFacetSpecs[f].id, kFacetSpecs[f].name, kFacetSpecs[f].unit,
            ds::metrics::MetricRange::at_least(0.0));
    }

    // 以目前一份流量填初值，並依其介面建立每介面實例（初建亦把有效值推入歷史，與後續
    // 採集路徑一致）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    for (int f = 0; f < kFacetCount; ++f) {
        registry.register_metric(metrics_[f]);
    }
}

void NetworkTrafficProvider::sample() {
    if (!metrics_[0]) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

std::size_t NetworkTrafficProvider::ensure_interface(const std::string& name) {
    auto it = iface_index_.find(name);
    if (it != iface_index_.end()) {
        return it->second;
    }
    const std::size_t idx = iface_names_.size();
    iface_names_.push_back(name);
    iface_index_.emplace(name, idx);
    // 於全部六個指標新增同名實例（label = 介面名）。unique_ptr 持有，既有參照不失效。
    for (int f = 0; f < kFacetCount; ++f) {
        insts_[f].push_back(&metrics_[f]->add_instance(name, name, history_));
    }
    return idx;
}

void NetworkTrafficProvider::write_value(ds::metrics::InMemoryMetricInstance* inst,
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

void NetworkTrafficProvider::apply(const NetTrafficSample& traffic, bool to_history) {
    if (!traffic.valid) {
        // 整體無讀值：全部既有介面實例設未知（不推歷史），不新增介面。
        for (int f = 0; f < kFacetCount; ++f) {
            for (auto* inst : insts_[f]) {
                write_value(inst, 0.0, /*valid=*/false, to_history);
            }
        }
        return;
    }

    // 先確保本次出現的介面都有實例（動態新增），並記錄本次出現的索引集合。
    std::vector<bool> seen(iface_names_.size(), false);
    for (const auto& iface : traffic.interfaces) {
        const std::size_t idx = ensure_interface(iface.name);
        if (idx >= seen.size()) seen.resize(idx + 1, false);
        seen[idx] = true;
        for (int f = 0; f < kFacetCount; ++f) {
            const auto vv = facet_value(iface, f);
            write_value(insts_[f][idx], vv.first, vv.second, to_history);
        }
    }

    // 本次未出現的既有介面（下線 / 無讀值）→ 全面向設未知（不縮減、誠實表達下線）。
    for (std::size_t idx = 0; idx < iface_names_.size(); ++idx) {
        if (idx < seen.size() && seen[idx]) continue;
        for (int f = 0; f < kFacetCount; ++f) {
            write_value(insts_[f][idx], 0.0, /*valid=*/false, to_history);
        }
    }
}

}  // namespace ds::sysinfo
