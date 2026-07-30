// E2-16 已安裝應用列舉 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：列舉來源、以 E2-01 記憶體內實作把每個應用建成一個實例、掛上註冊表。
// 無 `#ifdef`、無系統呼叫、無真實後端——換平台一行不動。
#include "installed_apps.hpp"

#include <memory>

namespace ds::sysinfo {

void InstalledAppsProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // 純計數：無單位、下界 0、上無界。
    auto metric = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, /*unit=*/"", ds::metrics::MetricRange::at_least(0));

    // source 為 null → 空清單（保守，不崩）。相位 1 的 null 後端亦回空清單。
    if (source_) {
        for (const auto& app : source_->enumerate()) {
            // 每個已安裝應用 = 一個可列舉實例：
            //   instance_id = 應用穩定識別碼、label = 顯示名。
            //   應用無時序歷史，故 history_capacity = 0（誠實表達「無歷史」）。
            auto& inst = metric->add_instance(app.id, app.name, /*history_capacity=*/0);
            // value：存在(1.0) + 版本文字（"" 代表版本未知）。
            //   用 set_value（不推歷史）——與 capacity 0 一致。
            inst.set_value(ds::metrics::MetricValue::of(1.0, app.version));
        }
    }

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(std::move(metric));
}

}  // namespace ds::sysinfo
