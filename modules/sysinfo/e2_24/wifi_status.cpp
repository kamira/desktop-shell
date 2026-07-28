// E2-24 Wi-Fi 狀態 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：直接讀（非差分）的 Wi-Fi 狀態快照 → 以 E2-01 記憶體內實作把各面向掛成單一
// Wi-Fi 介面的可列舉實例、掛上註冊表，並支援 sample() 重新讀取更新（數值面向推入歷史）。
// 無 `#ifdef`、無系統呼叫、無真實 Wi-Fi API（無 CoreWLAN / airport / CWInterface）
// ——換平台一行不動。
#include "wifi_status.hpp"

#include <algorithm>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// 列舉 → 字串（診斷 / 顯示）
// ---------------------------------------------------------------------------
const char* to_string(WifiBand band) noexcept {
    switch (band) {
        case WifiBand::Unknown:    return "unknown";
        case WifiBand::Band2_4GHz: return "2.4 GHz";
        case WifiBand::Band5GHz:   return "5 GHz";
        case WifiBand::Band6GHz:   return "6 GHz";
    }
    return "unknown";  // 不可達；防禦性
}

const char* to_string(WifiSecurity security) noexcept {
    switch (security) {
        case WifiSecurity::Unknown:    return "unknown";
        case WifiSecurity::Open:       return "open";
        case WifiSecurity::WEP:        return "wep";
        case WifiSecurity::WPA:        return "wpa";
        case WifiSecurity::WPA2:       return "wpa2";
        case WifiSecurity::WPA3:       return "wpa3";
        case WifiSecurity::Enterprise: return "enterprise";
    }
    return "unknown";  // 不可達
}

// ---------------------------------------------------------------------------
// rssi_to_percent（自由函式，獨立可測；純算術）
// ---------------------------------------------------------------------------
double rssi_to_percent(double dbm) noexcept {
    // -100 dBm → 0%、-50 dBm → 100%，其間線性；超界夾到 [0,100]。
    double pct = 2.0 * (dbm + 100.0);
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    return pct;
}

// ---------------------------------------------------------------------------
// WifiStatus 工廠
// ---------------------------------------------------------------------------
WifiStatus WifiStatus::connected_to(std::string ssid, double rssi_dbm, double link_rate_mbps,
                                    int channel, WifiBand band, WifiSecurity security) {
    WifiStatus s;
    s.connected = true;
    s.ssid = std::move(ssid);
    s.rssi_dbm = rssi_dbm;
    s.signal_percent = rssi_to_percent(rssi_dbm);
    s.link_rate_mbps = link_rate_mbps;
    s.channel = channel;
    s.band = band;
    s.security = security;
    s.valid = true;
    return s;
}

// ---------------------------------------------------------------------------
// NullWifiSource
// ---------------------------------------------------------------------------
WifiStatus NullWifiSource::sample() {
    if (sequence_.empty()) {
        return WifiStatus::unknown();  // 空列 → 無讀值
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx = std::min(cursor_, sequence_.size() - 1);
    if (cursor_ < sequence_.size()) ++cursor_;
    return sequence_[idx];
}

// ---------------------------------------------------------------------------
// WifiStatusProvider
// ---------------------------------------------------------------------------
std::vector<ds::metrics::MetricId> WifiStatusProvider::metric_ids() {
    return {
        kConnectedMetricId, kSsidMetricId, kRssiMetricId,   kSignalMetricId,
        kRateMetricId,      kChannelMetricId, kSecurityMetricId,
    };
}

void WifiStatusProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    using ds::metrics::InMemoryMetric;
    using ds::metrics::MetricRange;

    // 各面向的身分 / 單位 / 範圍（完全沿用 E2-01 六要素，不自造模型）。
    metrics_[kConnected] = std::make_shared<InMemoryMetric>(
        kConnectedMetricId, "Wi-Fi Connected", "", MetricRange::bounded(0.0, 1.0));
    metrics_[kSsid] = std::make_shared<InMemoryMetric>(
        kSsidMetricId, "Wi-Fi SSID", "", MetricRange::unbounded());
    metrics_[kRssi] = std::make_shared<InMemoryMetric>(
        kRssiMetricId, "Wi-Fi Signal (RSSI)", kRssiUnit, MetricRange::bounded(-100.0, 0.0));
    metrics_[kSignal] = std::make_shared<InMemoryMetric>(
        kSignalMetricId, "Wi-Fi Signal", kSignalUnit, MetricRange::bounded(0.0, 100.0));
    metrics_[kRate] = std::make_shared<InMemoryMetric>(
        kRateMetricId, "Wi-Fi Link Rate", kRateUnit, MetricRange::at_least(0.0));
    metrics_[kChannel] = std::make_shared<InMemoryMetric>(
        kChannelMetricId, "Wi-Fi Channel", "", MetricRange::at_least(0.0));
    metrics_[kSecurity] = std::make_shared<InMemoryMetric>(
        kSecurityMetricId, "Wi-Fi Security", "", MetricRange::unbounded());

    // 每個指標建單一 Wi-Fi 介面實例（列舉唯一實例）。
    for (std::size_t i = 0; i < kFacetCount; ++i) {
        insts_[i] = &metrics_[i]->add_instance(kInstanceId, kInstanceLabel, history_);
    }

    // 以目前一份狀態填初值（初建亦把有效數值面向推入歷史，與後續採集路徑一致）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表（註冊順序 = Facet 順序）；重複 id 由註冊表保守拒絕（不覆寫既有）。
    for (std::size_t i = 0; i < kFacetCount; ++i) {
        registry.register_metric(metrics_[i]);
    }
}

void WifiStatusProvider::sample() {
    if (!metrics_[kConnected]) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

void WifiStatusProvider::write(Facet facet, const ds::metrics::MetricValue& v, bool to_history,
                               bool push_history) {
    ds::metrics::InMemoryMetricInstance* inst = insts_[facet];
    if (!inst) return;
    if (!v.valid) {
        // 無讀值：設為未知且不推入歷史（不污染序列），保守不謊報 0。
        inst->set_value(ds::metrics::MetricValue::unknown());
        return;
    }
    if (to_history && push_history) {
        inst->update(v);  // 推入歷史（update 於 valid 值才推）
    } else {
        inst->set_value(v);
    }
}

void WifiStatusProvider::apply(const WifiStatus& status, bool to_history) {
    using ds::metrics::MetricValue;

    // 「是否已連線」面向：整體有讀值即有效（值 = 1|0），否則未知。此面向不受 connected
    // 影響——未連線時它照樣有效（誠實回報「未連線」），是數值面向、推入歷史。
    if (status.valid) {
        const double c = status.connected ? 1.0 : 0.0;
        write(kConnected, MetricValue::of(c, status.connected ? "Connected" : "Disconnected"),
              to_history, /*push_history=*/true);
    } else {
        write(kConnected, MetricValue::unknown(), to_history, /*push_history=*/true);
    }

    // 連線相依面向：僅在「整體有讀值且已連線」時有效；否則未知（未連線即無這些讀值）。
    const bool linked = status.valid && status.connected;

    // SSID：categorical（文字表述），數值維度佔位 0；不推入歷史（數值無時序意義）。
    write(kSsid,
          linked ? MetricValue::of(0.0, status.ssid) : MetricValue::unknown(),
          to_history, /*push_history=*/false);

    // RSSI（dBm）：數值面向，推入歷史。
    write(kRssi, linked ? MetricValue::of(status.rssi_dbm) : MetricValue::unknown(),
          to_history, /*push_history=*/true);

    // 訊號百分比：數值面向，推入歷史。
    write(kSignal, linked ? MetricValue::of(status.signal_percent) : MetricValue::unknown(),
          to_history, /*push_history=*/true);

    // 連線速率（Mbps）：數值面向，推入歷史。
    write(kRate, linked ? MetricValue::of(status.link_rate_mbps) : MetricValue::unknown(),
          to_history, /*push_history=*/true);

    // 頻道（+頻段文字）：數值面向，推入歷史。
    write(kChannel,
          linked ? MetricValue::of(static_cast<double>(status.channel), to_string(status.band))
                 : MetricValue::unknown(),
          to_history, /*push_history=*/true);

    // 安全類型：categorical（列舉 ordinal + 文字表述）；不推入歷史。
    write(kSecurity,
          linked ? MetricValue::of(static_cast<double>(static_cast<int>(status.security)),
                                   to_string(status.security))
                 : MetricValue::unknown(),
          to_history, /*push_history=*/false);
}

}  // namespace ds::sysinfo
