// E2-17 主機板感測器（溫度 / 風扇 / 電壓）— 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：注入式來源列舉全部感測器、依類型分三組（溫度 / 風扇 / 電壓），每顆給一份帶
// valid 的讀值；以 E2-01 記憶體內實作把每顆感測器建成對應指標的可列舉實例、掛上註冊表，
// 並支援 sample() 重新讀取更新（有效值推入歷史）。無 `#ifdef`、無系統呼叫、無真實感測器
// API（無 SMC / lm-sensors / IOKit）——換平台一行不動。
#include "board_sensors.hpp"

#include <string>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// 類型輔助（獨立可測，平台中立）
// ---------------------------------------------------------------------------
const char* unit_for(SensorType type) noexcept {
    switch (type) {
        case SensorType::Temperature:
            return "°C";
        case SensorType::Fan:
            return "RPM";
        case SensorType::Voltage:
            return "V";
    }
    return "";  // 不可達（列舉窮舉）；保守回空字串
}

const char* to_string(SensorType type) noexcept {
    switch (type) {
        case SensorType::Temperature:
            return "temperature";
        case SensorType::Fan:
            return "fan";
        case SensorType::Voltage:
            return "voltage";
    }
    return "unknown";  // 不可達（列舉窮舉）
}

// ---------------------------------------------------------------------------
// NullBoardSensorSource
// ---------------------------------------------------------------------------
BoardSensorSample NullBoardSensorSource::sample() {
    if (sequence_.empty()) {
        return BoardSensorSample{};  // 空列 → 空快照（無感測器 / 無讀值）
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx =
        (cursor_ < sequence_.size()) ? cursor_ : sequence_.size() - 1;
    if (cursor_ < sequence_.size()) ++cursor_;
    return sequence_[idx];
}

// ---------------------------------------------------------------------------
// BoardSensorsProvider
// ---------------------------------------------------------------------------
namespace {
// 第 i 顆感測器的穩定 instance_id（如 "temp0"）。
std::string sensor_id(const char* prefix, std::size_t i) {
    return std::string(prefix) + std::to_string(i);
}
// 第 i 顆感測器的 label：有名稱用名稱；未命名以類型前綴 + 序號補位（如 "Temp 0"）。
std::string sensor_label(const char* label_prefix, const std::string& name, std::size_t i) {
    if (!name.empty()) return name;
    return std::string(label_prefix) + " " + std::to_string(i);
}
}  // namespace

void BoardSensorsProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    using ds::metrics::InMemoryMetric;
    using ds::metrics::MetricRange;

    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // 溫度 / 風扇：值域下界 0、上不設假界（溫度 / 轉速無自然上限）。
    // 電壓：上下皆不設界（電壓軌可為負軌如 -12V）。
    temp_.metric = std::make_shared<InMemoryMetric>(
        kTempId, kTempName, kTempUnit, MetricRange::at_least(0.0));
    fan_.metric = std::make_shared<InMemoryMetric>(
        kFanId, kFanName, kFanUnit, MetricRange::at_least(0.0));
    voltage_.metric = std::make_shared<InMemoryMetric>(
        kVoltageId, kVoltageName, kVoltageUnit, MetricRange::unbounded());

    // 以目前一份讀值填初值，並依其各類感測器數建立實例（初建亦把有效值推入歷史，與後續
    // 採集路徑一致）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。三個各自註冊。
    registry.register_metric(temp_.metric);
    registry.register_metric(fan_.metric);
    registry.register_metric(voltage_.metric);
}

void BoardSensorsProvider::sample() {
    if (!temp_.metric) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

void BoardSensorsProvider::ensure_instances(Group& g,
                                            const std::vector<SensorReading>& readings,
                                            std::size_t count) {
    for (std::size_t i = g.insts.size(); i < count; ++i) {
        const std::string id = sensor_id(g.id_prefix, i);
        // 名稱取自對應讀值（若該序號存在），未命名則以類型前綴 + 序號補位。
        const std::string& name = (i < readings.size()) ? readings[i].name : std::string();
        const std::string label = sensor_label(g.label_prefix, name, i);
        g.insts.push_back(&g.metric->add_instance(id, label, history_));
    }
}

void BoardSensorsProvider::write_value(ds::metrics::InMemoryMetricInstance* inst, double number,
                                       bool valid, bool to_history) {
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

void BoardSensorsProvider::apply_group(Group& g, const std::vector<SensorReading>& readings,
                                       bool to_history) {
    const std::size_t n = readings.size();

    // 必要時動態擴增實例（新取樣感測器數多於既有）——unique_ptr 持有，既有參照不失效。
    ensure_instances(g, readings, n);

    for (std::size_t i = 0; i < g.insts.size(); ++i) {
        if (i < n) {
            write_value(g.insts[i], readings[i].value, readings[i].valid, to_history);
        } else {
            // 本次取樣少了這顆感測器（下線 / 無讀值）→ 設未知、不推歷史。
            write_value(g.insts[i], 0.0, /*valid=*/false, to_history);
        }
    }
}

void BoardSensorsProvider::apply(const BoardSensorSample& sample, bool to_history) {
    apply_group(temp_, sample.temperatures, to_history);
    apply_group(fan_, sample.fans, to_history);
    apply_group(voltage_, sample.voltages, to_history);
}

}  // namespace ds::sysinfo
