// E2-12 系統靜態資訊 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：隨選查詢來源、以 E2-01 記憶體內實作把每個欄位建成一個實例、掛上註冊表。
// 無 `#ifdef`、無系統呼叫、無真實後端——換平台一行不動。
#include "system_info.hpp"

#include <memory>

namespace ds::sysinfo {

void SystemInfoProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // 異質靜態欄位集：無統一單位、無值域（unbounded）。
    auto metric = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, /*unit=*/"", ds::metrics::MetricRange::unbounded());

    // source 為 null → 空欄位集（保守，不崩）。相位 1 的 null 後端亦回空欄位集。
    if (source_) {
        for (const auto& field : source_->query()) {
            // 每個靜態欄位 = 一個可列舉實例：
            //   instance_id = 欄位鍵、label = 顯示名。
            //   靜態資訊無時序歷史，故 history_capacity = 0（誠實表達「無歷史」）。
            auto& inst = metric->add_instance(field.key, field.label, /*history_capacity=*/0);
            // value：數值維度 + 文字值（純文字欄位 number 為 0.0，消費者讀 text）。
            //   用 set_value（不推歷史）——與 capacity 0 一致。
            inst.set_value(ds::metrics::MetricValue::of(field.number, field.text));
        }
    }

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(std::move(metric));
}

}  // namespace ds::sysinfo
