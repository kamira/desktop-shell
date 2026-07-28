// E2-05 GPU 使用率 / VRAM / 溫度 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：注入式來源列舉 GPU、每張給 usage/vram/temp（各帶 valid）；以 E2-01 記憶體內
// 實作把每張 GPU 建成三個指標的可列舉實例、掛上註冊表，並支援 sample() 重新讀取更新
// （有效值推入歷史）。無 `#ifdef`、無系統呼叫、無真實 GPU API（無 Metal / IOKit / nvml）
// ——換平台一行不動。
#include "gpu_stats.hpp"

#include <algorithm>
#include <string>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// 自由函式（獨立可測，平台中立純算術 / 格式化）
// ---------------------------------------------------------------------------
double vram_ratio(std::uint64_t used, std::uint64_t total) noexcept {
    if (total == 0) {
        return 0.0;  // 無總量，無從判斷，不謊報
    }
    if (used >= total) {
        return 1.0;  // 夾到滿載（used>total 理論不該發生）
    }
    return static_cast<double>(used) / static_cast<double>(total);
}

std::string humanize_bytes(std::uint64_t bytes) {
    // 二進位級距（KiB/MiB/GiB/TiB）。>=1 個單位才升級，否則以更小單位整數呈現。
    constexpr std::uint64_t kKi = 1024ULL;
    constexpr std::uint64_t kMi = kKi * 1024ULL;
    constexpr std::uint64_t kGi = kMi * 1024ULL;
    constexpr std::uint64_t kTi = kGi * 1024ULL;

    std::uint64_t unit = 1;
    const char* suffix = "B";
    if (bytes >= kTi) {
        unit = kTi;
        suffix = "TiB";
    } else if (bytes >= kGi) {
        unit = kGi;
        suffix = "GiB";
    } else if (bytes >= kMi) {
        unit = kMi;
        suffix = "MiB";
    } else if (bytes >= kKi) {
        unit = kKi;
        suffix = "KiB";
    }

    if (unit == 1) {
        return std::to_string(bytes) + " B";  // 未達 KiB：整數 bytes
    }
    // 一位小數（*10 後取整，避免依賴 <sstream> / locale）。
    const std::uint64_t scaled = (bytes * 10ULL) / unit;  // = value*10
    const std::uint64_t whole = scaled / 10ULL;
    const std::uint64_t frac = scaled % 10ULL;
    return std::to_string(whole) + "." + std::to_string(frac) + " " + suffix;
}

// ---------------------------------------------------------------------------
// NullGpuStatSource
// ---------------------------------------------------------------------------
GpuStatSample NullGpuStatSource::sample() {
    if (sequence_.empty()) {
        return GpuStatSample{};  // 空列 → 空快照（無 GPU / 無讀值）
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx = std::min(cursor_, sequence_.size() - 1);
    if (cursor_ < sequence_.size()) ++cursor_;
    return sequence_[idx];
}

// ---------------------------------------------------------------------------
// GpuStatsProvider
// ---------------------------------------------------------------------------
namespace {
// 生成第 i 張 GPU 的穩定 instance_id（"gpu0" / "gpu1" / …）與 label（"GPU 0" / …）。
std::string gpu_id(std::size_t i) { return "gpu" + std::to_string(i); }
std::string gpu_label(std::size_t i) { return "GPU " + std::to_string(i); }
}  // namespace

void GpuStatsProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    using ds::metrics::InMemoryMetric;
    using ds::metrics::MetricRange;

    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // 使用率 / VRAM：單位 "%"、值域有界 [0,100]（可正規化到 [0,1] 供長條 / 直方圖）。
    // 溫度：單位 "°C"、值域下界 0、上不設假界（溫度無自然上限）。
    usage_metric_ = std::make_shared<InMemoryMetric>(
        kUsageId, kUsageName, kPercentUnit, MetricRange::bounded(0.0, 100.0));
    vram_metric_ = std::make_shared<InMemoryMetric>(
        kVramId, kVramName, kPercentUnit, MetricRange::bounded(0.0, 100.0));
    temp_metric_ = std::make_shared<InMemoryMetric>(
        kTempId, kTempName, kTempUnit, MetricRange::at_least(0.0));

    // 以目前一份讀值填初值，並依其 GPU 數建立每張 GPU 的實例（初建亦把有效值推入歷史，
    // 與後續採集路徑一致）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。三個各自註冊。
    registry.register_metric(usage_metric_);
    registry.register_metric(vram_metric_);
    registry.register_metric(temp_metric_);
}

void GpuStatsProvider::sample() {
    if (!usage_metric_) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

void GpuStatsProvider::ensure_gpu_instances(std::size_t count) {
    for (std::size_t i = usage_insts_.size(); i < count; ++i) {
        const std::string id = gpu_id(i);
        const std::string label = gpu_label(i);
        usage_insts_.push_back(&usage_metric_->add_instance(id, label, history_));
        vram_insts_.push_back(&vram_metric_->add_instance(id, label, history_));
        temp_insts_.push_back(&temp_metric_->add_instance(id, label, history_));
    }
}

void GpuStatsProvider::write_value(ds::metrics::InMemoryMetricInstance* inst, double number,
                                   bool valid, bool to_history, std::string text) {
    using ds::metrics::MetricValue;
    if (!valid) {
        // 無讀值：設為未知且**不**推入歷史（不污染序列），保守不謊報 0。
        inst->set_value(MetricValue::unknown());
        return;
    }
    const MetricValue v = text.empty() ? MetricValue::of(number)
                                       : MetricValue::of(number, std::move(text));
    if (to_history) {
        inst->update(v);  // 推入歷史（update 於 valid 值才推）
    } else {
        inst->set_value(v);
    }
}

void GpuStatsProvider::apply(const GpuStatSample& sample, bool to_history) {
    const std::size_t n = sample.gpu_count();

    // 必要時動態擴增 GPU 實例（新取樣 GPU 數多於既有）——unique_ptr 持有，既有參照不失效。
    ensure_gpu_instances(n);

    for (std::size_t i = 0; i < usage_insts_.size(); ++i) {
        if (i < n) {
            const GpuStat& g = sample.gpus[i];
            // 使用率：比率 [0,1] → %。
            write_value(usage_insts_[i], g.usage * kPercentScale, g.usage_valid, to_history);
            // VRAM：主維度為使用率 %；已用/總量 bytes 置於 text 供顯示。
            // 總量 0 視為無讀值（百分比無從計算，不謊報 0）。
            const bool vram_ok = g.vram_valid && g.vram_total > 0;
            const double vram_pct = vram_ratio(g.vram_used, g.vram_total) * kPercentScale;
            std::string vram_text;
            if (vram_ok) {
                vram_text = humanize_bytes(g.vram_used) + " / " + humanize_bytes(g.vram_total);
            }
            write_value(vram_insts_[i], vram_pct, vram_ok, to_history, std::move(vram_text));
            // 溫度：°C 直接寫。
            write_value(temp_insts_[i], g.temperature, g.temp_valid, to_history);
        } else {
            // 本次取樣少了這張 GPU（下線 / 無讀值）→ 三欄皆設未知、不推歷史。
            write_value(usage_insts_[i], 0.0, /*valid=*/false, to_history);
            write_value(vram_insts_[i], 0.0, /*valid=*/false, to_history);
            write_value(temp_insts_[i], 0.0, /*valid=*/false, to_history);
        }
    }
}

}  // namespace ds::sysinfo
