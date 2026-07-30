// E2-25 剪貼簿監看 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：掛上 E2-01 記憶體內指標；每次 sample() 讀來源、經 ClipboardMonitor 偵測變更、
// 更新實例 value 與歷史。無 `#ifdef`、無系統呼叫、無真實後端——換平台一行不動。
#include "clipboard_monitor.hpp"

namespace ds::sysinfo {

void ClipboardMonitorProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // 數值維度為累計變更次數：無單位、下界 0、上無界（單調不減）。
    auto metric = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, /*unit=*/"", ds::metrics::MetricRange::at_least(0));

    // 剪貼簿為單值來源 → 單一實例（沿用 E2-01 的單值實例慣例 id ""）。
    auto& inst = metric->add_instance(ds::metrics::Metric::kSingleInstanceId,
                                      /*label=*/kMetricName, history_capacity_);
    // 初始 value 為 unknown：尚未採集，不把 0 誤當真實讀值。由 sample() 驅動更新。
    inst.set_value(ds::metrics::MetricValue::unknown());

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    if (registry.register_metric(metric)) {
        // 僅在成功掛上後才保留參照，讓 sample() 更新的是註冊表中的同一物件。
        metric_ = std::move(metric);
        instance_ = &inst;
    } else {
        // 掛上失敗（同 id 已存在）：不保留參照，sample() 隨即成為 no-op（保守不崩）。
        metric_ = nullptr;
        instance_ = nullptr;
    }
}

bool ClipboardMonitorProvider::sample() {
    // 未掛上指標（未 register 或註冊被拒）→ no-op。
    if (instance_ == nullptr) {
        return false;
    }

    // source 為 null → 保守視為空剪貼簿（不崩、不讀系統）。
    const ClipboardSnapshot snap =
        source_ ? source_->read() : ClipboardSnapshot::empty();

    // 純邏輯變更偵測。
    const bool changed = monitor_.observe(snap);

    // 更新指標 value：
    //   number = 累計變更次數（可繪 sparkline）
    //   text   = 目前內容（空剪貼簿為 ""）
    //   valid  = true（已採集一次；空剪貼簿仍是「有效讀值＝目前為空」）
    // update() 同時把 number 推入歷史環，形成變更活動的時序。
    ds::metrics::MetricValue v = ds::metrics::MetricValue::of(
        static_cast<double>(monitor_.change_count()),
        snap.present ? snap.text : std::string{});
    instance_->update(v);

    return changed;
}

}  // namespace ds::sysinfo
