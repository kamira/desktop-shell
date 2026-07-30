// E2-04 記憶體使用量（實體/交換）— 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：以 E2-01 記憶體內實作把各記憶體欄位（實體總量／已用／可用／使用率 % + swap
// 總量／已用）建成固定的可列舉實例、掛上註冊表，並支援 sample() 重新讀取更新（變動欄位
// 推入歷史）。使用率由 used/total 純算術得出並夾到 [0,100]。無 `#ifdef`、無系統呼叫、
// 無真實記憶體 API（無 host_statistics / sysctl / /proc/meminfo / mach）——換平台一行不動。
#include "memory_usage.hpp"

#include <algorithm>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// MemoryStats
// ---------------------------------------------------------------------------
MemoryStats MemoryStats::from_physical(std::uint64_t total, std::uint64_t used,
                                       std::uint64_t swap_total, std::uint64_t swap_used) {
    MemoryStats s;
    s.physical_total = total;
    s.physical_used = used;
    // 可用量 = total - used（夾到 >=0；used 不該超過 total，但誠實防護）。
    s.physical_available = used <= total ? total - used : 0;
    s.swap_total = swap_total;
    s.swap_used = swap_used;
    s.valid = true;
    return s;
}

namespace {
// 佔用比率 % = used / total * 100，夾到 [0,100]；total==0 → 0（無從判斷，不謊報）。
double ratio_percent(std::uint64_t used, std::uint64_t total) noexcept {
    if (total == 0) return 0.0;
    double p = static_cast<double>(used) / static_cast<double>(total) * 100.0;
    if (p < 0.0) p = 0.0;
    if (p > 100.0) p = 100.0;  // used > total（理論不該發生）→ 夾到滿載
    return p;
}
}  // namespace

double MemoryStats::usage_percent() const noexcept {
    return ratio_percent(physical_used, physical_total);
}

double MemoryStats::swap_usage_percent() const noexcept {
    return ratio_percent(swap_used, swap_total);
}

// ---------------------------------------------------------------------------
// NullMemoryStatSource
// ---------------------------------------------------------------------------
MemoryStats NullMemoryStatSource::read() {
    if (sequence_.empty()) {
        return MemoryStats::unknown();  // 空列 → 無讀值
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx = std::min(cursor_, sequence_.size() - 1);
    if (cursor_ < sequence_.size()) ++cursor_;
    return sequence_[idx];
}

// ---------------------------------------------------------------------------
// MemoryProvider
// ---------------------------------------------------------------------------
void MemoryProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。欄位異質（bytes 與 %）：
    // metric 層 unit=""、range=unbounded；各欄位自帶語意（見 kUnit* / MetricValue 文字維度）。
    metric_ = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, /*unit=*/"", ds::metrics::MetricRange::unbounded());

    // 固定欄位集（列舉順序決定性）。靜態欄位（實體總量 / swap 總量）無歷史（capacity==0）；
    // 變動欄位（已用 / 可用 / 使用率 % / swap 已用）保留歷史環（配合 E2-02 週期採集鋪序列）。
    inst_phys_total_ = &metric_->add_instance(kFieldPhysicalTotal, kPhysicalTotalLabel, 0);
    inst_phys_used_ = &metric_->add_instance(kFieldPhysicalUsed, kPhysicalUsedLabel, history_);
    inst_phys_avail_ =
        &metric_->add_instance(kFieldPhysicalAvailable, kPhysicalAvailableLabel, history_);
    inst_usage_ = &metric_->add_instance(kFieldUsagePercent, kUsagePercentLabel, history_);
    inst_swap_total_ = &metric_->add_instance(kFieldSwapTotal, kSwapTotalLabel, 0);
    inst_swap_used_ = &metric_->add_instance(kFieldSwapUsed, kSwapUsedLabel, history_);

    // 以目前一份用量填初值（初建亦把有效值推入歷史，與後續採集路徑一致）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(metric_);
}

void MemoryProvider::sample() {
    if (!metric_) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

void MemoryProvider::write_bytes(ds::metrics::InMemoryMetricInstance* inst, double bytes,
                                 bool valid, bool to_history) {
    using ds::metrics::MetricValue;
    if (!valid) {
        // 無讀值：設為未知且**不**推入歷史（不污染序列），保守不謊報 0。
        inst->set_value(MetricValue::unknown());
        return;
    }
    const MetricValue v = MetricValue::of(bytes);
    if (to_history) {
        inst->update(v);  // 推入歷史（update 於 valid 值才推）
    } else {
        inst->set_value(v);
    }
}

void MemoryProvider::write_percent(ds::metrics::InMemoryMetricInstance* inst, double percent,
                                   bool valid, bool to_history) {
    using ds::metrics::MetricValue;
    if (!valid) {
        inst->set_value(MetricValue::unknown());
        return;
    }
    // 使用率以數值維度承載（單位 % 見 kUnitPercent）；消費者讀 number 繪長條 / 儀表。
    const MetricValue v = MetricValue::of(percent);
    if (to_history) {
        inst->update(v);
    } else {
        inst->set_value(v);
    }
}

void MemoryProvider::apply(const MemoryStats& stats, bool to_history) {
    const bool ok = stats.valid;

    // 實體記憶體 bytes 欄位：valid 決定是否為未知。
    write_bytes(inst_phys_total_, static_cast<double>(stats.physical_total), ok, to_history);
    write_bytes(inst_phys_used_, static_cast<double>(stats.physical_used), ok, to_history);
    write_bytes(inst_phys_avail_, static_cast<double>(stats.physical_available), ok, to_history);

    // 使用率 %：僅在有讀值且總量 > 0 時可算（total==0 無從判斷 → 未知，不謊報 0）。
    const bool usage_ok = ok && stats.physical_total > 0;
    write_percent(inst_usage_, stats.usage_percent(), usage_ok, to_history);

    // swap bytes 欄位：valid 決定未知（swap_total==0 = 無 swap，仍為有效讀值 0）。
    write_bytes(inst_swap_total_, static_cast<double>(stats.swap_total), ok, to_history);
    write_bytes(inst_swap_used_, static_cast<double>(stats.swap_used), ok, to_history);
}

}  // namespace ds::sysinfo
