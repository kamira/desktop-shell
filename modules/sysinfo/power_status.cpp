// E2-09 電源與電池狀態 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：以注入式來源讀電源 / 電池狀態、以 E2-01 記憶體內實作把各維度建成單一實例指標、
// 掛上註冊表，並支援 sample() 重新讀取更新（有效值推入歷史）。無 `#ifdef`、無系統呼叫、
// 無真實電源 API（無 IOKit / IOPowerSources / GetSystemPowerStatus）——換平台一行不動。
#include "power_status.hpp"

#include <string>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// ChargeState
// ---------------------------------------------------------------------------
const char* to_string(ChargeState state) noexcept {
    switch (state) {
        case ChargeState::NoBattery:   return "no-battery";
        case ChargeState::Discharging: return "discharging";
        case ChargeState::Charging:    return "charging";
        case ChargeState::Full:        return "full";
    }
    return "no-battery";  // 不可達；保守回退
}

std::string charge_state_label(ChargeState state) {
    switch (state) {
        case ChargeState::NoBattery:   return "No Battery";
        case ChargeState::Discharging: return "Discharging";
        case ChargeState::Charging:    return "Charging";
        case ChargeState::Full:        return "Full";
    }
    return "No Battery";  // 不可達；保守回退
}

// ---------------------------------------------------------------------------
// SequencedPowerStatSource
// ---------------------------------------------------------------------------
PowerStatus SequencedPowerStatSource::sample() {
    if (sequence_.empty()) {
        return PowerStatus::unknown();  // 空列 → 無讀值
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx =
        (cursor_ < sequence_.size()) ? cursor_ : sequence_.size() - 1;
    if (cursor_ < sequence_.size()) ++cursor_;
    return sequence_[idx];
}

// ---------------------------------------------------------------------------
// PowerProvider
// ---------------------------------------------------------------------------
PowerProvider::MetricSlot PowerProvider::make_metric(
    ds::metrics::MetricRegistry& registry, const char* id, std::string name,
    std::string unit, ds::metrics::MetricRange range) {
    MetricSlot slot;
    slot.metric = std::make_shared<ds::metrics::InMemoryMetric>(
        id, std::move(name), std::move(unit), range);
    // 單一實例（kSingleInstanceId=""）。label 沿用指標名（供掛件顯示）。
    slot.inst = &slot.metric->add_instance(
        ds::metrics::Metric::kSingleInstanceId, slot.metric->name(), history_);
    registry.register_metric(slot.metric);
    return slot;
}

void PowerProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    using ds::metrics::MetricRange;

    // 各維度單位 / 值域不同，故各為獨立的單一實例指標（非同一指標的多實例）。
    // 電量 %：有界 [0,100]（可正規化到 [0,1] 供長條 / 直方圖）。
    level_  = make_metric(registry, kLevelId, "Battery Level", "%",
                          MetricRange::bounded(0.0, 100.0));
    // 充電狀態：類別型（number = ChargeState 碼、text = 顯示標籤），值域無界。
    state_  = make_metric(registry, kStateId, "Battery State", "",
                          MetricRange::unbounded());
    // 接電源：0 / 1（有界 [0,1]）。
    ac_     = make_metric(registry, kAcId, "AC Power", "",
                          MetricRange::bounded(0.0, 1.0));
    // 剩餘時間：分鐘（下界 0，上無界）。
    time_   = make_metric(registry, kTimeId, "Battery Time Remaining", "min",
                          MetricRange::at_least(0.0));
    // 循環次數（可選）：下界 0，上無界。
    cycles_ = make_metric(registry, kCyclesId, "Battery Cycle Count", "",
                          MetricRange::at_least(0.0));
    // 健康度（可選）：% 有界 [0,100]。
    health_ = make_metric(registry, kHealthId, "Battery Health", "%",
                          MetricRange::bounded(0.0, 100.0));

    registered_ = true;

    // 以目前一份狀態填初值（初建亦把有效值推入歷史，與後續採集路徑一致）。
    apply(current(), /*to_history=*/true);
}

void PowerProvider::sample() {
    if (!registered_) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

void PowerProvider::write(MetricSlot& slot, const ds::metrics::MetricValue& v,
                          bool to_history) {
    if (slot.inst == nullptr) return;
    if (!v.valid) {
        // 無讀值：設為未知且**不**推入歷史（不污染序列），保守不謊報。
        slot.inst->set_value(ds::metrics::MetricValue::unknown());
        return;
    }
    if (to_history) {
        slot.inst->update(v);  // 推入歷史（update 於 valid 值才推）
    } else {
        slot.inst->set_value(v);
    }
}

void PowerProvider::apply(const PowerStatus& status, bool to_history) {
    using ds::metrics::MetricValue;

    // 電量 %：valid 且來源有 percent 才有讀值。
    if (status.valid && status.percent.has_value()) {
        write(level_, MetricValue::of(*status.percent), to_history);
    } else {
        write(level_, MetricValue::unknown(), to_history);
    }

    // 充電狀態：只要成功讀到電源系統（status.valid）即為有讀值（含 no-battery）。
    // number = 狀態碼、text = 顯示標籤（如 "Charging"）。
    if (status.valid) {
        write(state_,
              MetricValue::of(charge_state_code(status.state),
                              charge_state_label(status.state)),
              to_history);
    } else {
        write(state_, MetricValue::unknown(), to_history);
    }

    // 接電源：status.valid 即有讀值；number = 1/0、text = "Online"/"Offline"。
    if (status.valid) {
        const bool on = status.on_ac_power;
        write(ac_,
              MetricValue::of(on ? kAcOnline : kAcOffline,
                              on ? std::string("Online") : std::string("Offline")),
              to_history);
    } else {
        write(ac_, MetricValue::unknown(), to_history);
    }

    // 剩餘時間（分鐘）：valid 且來源有估計才有讀值。
    if (status.valid && status.minutes_remaining.has_value()) {
        write(time_, MetricValue::of(static_cast<double>(*status.minutes_remaining)),
              to_history);
    } else {
        write(time_, MetricValue::unknown(), to_history);
    }

    // 循環次數（可選）：valid 且來源提供才有讀值。
    if (status.valid && status.cycle_count.has_value()) {
        write(cycles_, MetricValue::of(static_cast<double>(*status.cycle_count)),
              to_history);
    } else {
        write(cycles_, MetricValue::unknown(), to_history);
    }

    // 健康度（可選）：valid 且來源提供才有讀值。
    if (status.valid && status.health_percent.has_value()) {
        write(health_, MetricValue::of(*status.health_percent), to_history);
    } else {
        write(health_, MetricValue::unknown(), to_history);
    }
}

}  // namespace ds::sysinfo
