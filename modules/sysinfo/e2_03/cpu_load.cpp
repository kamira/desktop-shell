// E2-03 CPU 負載（總體與每核心）— 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：兩次取樣差分算使用率（或直接讀比率）、以 E2-01 記憶體內實作把總體 + 每核心
// 建成可列舉實例、掛上註冊表，並支援 sample() 重新讀取更新（使用率推入歷史）。
// 無 `#ifdef`、無系統呼叫、無真實 CPU API（無 host_processor_info / /proc / mach）
// ——換平台一行不動。
#include "cpu_load.hpp"

#include <algorithm>
#include <string>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// 差分演算法（自由函式，獨立可測）
// ---------------------------------------------------------------------------
double core_usage_ratio(const CpuCoreTicks& prev, const CpuCoreTicks& curr) noexcept {
    // 計數器重置（累積值理應單調遞增；curr < prev = 重置 / 回繞）→ 保守回 0。
    if (curr.total < prev.total || curr.busy < prev.busy) {
        return 0.0;
    }
    const std::uint64_t total_delta = curr.total - prev.total;
    if (total_delta == 0) {
        return 0.0;  // 無經過時間，無從判斷
    }
    std::uint64_t busy_delta = curr.busy - prev.busy;
    if (busy_delta > total_delta) {
        busy_delta = total_delta;  // 理論不該發生；夾到滿載
    }
    return static_cast<double>(busy_delta) / static_cast<double>(total_delta);
}

CpuUsageSample usage_from_delta(const CpuTicksSample& prev, const CpuTicksSample& curr) {
    CpuUsageSample out;
    // 對齊到兩份的共同核心數（較小者）——核心數可在取樣間變動。
    const std::size_t n = std::min(prev.core_count(), curr.core_count());
    if (n == 0) {
        return out;  // 無可差分的核心 → 無讀值（valid==false）
    }

    out.per_core.reserve(n);
    std::uint64_t sum_busy_delta = 0;
    std::uint64_t sum_total_delta = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const CpuCoreTicks& p = prev.cores[i];
        const CpuCoreTicks& c = curr.cores[i];
        out.per_core.push_back(core_usage_ratio(p, c));
        // 總體以聚合差分（sum busyΔ / sum totalΔ），逐核累加（同樣防重置）。
        if (c.total >= p.total && c.busy >= p.busy) {
            const std::uint64_t td = c.total - p.total;
            std::uint64_t bd = c.busy - p.busy;
            if (bd > td) bd = td;
            sum_total_delta += td;
            sum_busy_delta += bd;
        }
    }
    out.overall = (sum_total_delta == 0)
                      ? 0.0
                      : static_cast<double>(sum_busy_delta) /
                            static_cast<double>(sum_total_delta);
    out.valid = true;  // 有 >=1 個共同核心可差分
    return out;
}

// ---------------------------------------------------------------------------
// NullCpuStatSource
// ---------------------------------------------------------------------------
void NullCpuStatSource::set_per_core(std::vector<double> ratios) {
    CpuUsageSample s;
    s.valid = true;
    double sum = 0.0;
    for (double r : ratios) sum += r;
    s.overall = ratios.empty() ? 0.0 : sum / static_cast<double>(ratios.size());
    s.per_core = std::move(ratios);
    fixed_ = std::move(s);
}

// ---------------------------------------------------------------------------
// NullCpuTickSource
// ---------------------------------------------------------------------------
CpuTicksSample NullCpuTickSource::read() {
    if (sequence_.empty()) {
        return CpuTicksSample{};  // 空列 → 空快照
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx = std::min(cursor_, sequence_.size() - 1);
    if (cursor_ < sequence_.size()) ++cursor_;
    return sequence_[idx];
}

// ---------------------------------------------------------------------------
// DifferencingCpuStatSource
// ---------------------------------------------------------------------------
CpuUsageSample DifferencingCpuStatSource::sample() {
    if (!ticks_) {
        return CpuUsageSample::unknown();  // 無 tick 來源 → 無讀值
    }
    CpuTicksSample curr = ticks_->read();
    if (!primed_) {
        // 首次取樣：只有一份，尚無可差分的基準 → 無讀值。保存為基準。
        prev_ = std::move(curr);
        primed_ = true;
        return CpuUsageSample::unknown();
    }
    CpuUsageSample usage = usage_from_delta(prev_, curr);
    prev_ = std::move(curr);  // 推進基準供下次差分
    return usage;
}

// ---------------------------------------------------------------------------
// CpuLoadProvider
// ---------------------------------------------------------------------------
namespace {
// 生成第 i 核的穩定 instance_id（"cpu0" / "cpu1" / …）與 label（"Core 0" / …）。
std::string core_id(std::size_t i) { return "cpu" + std::to_string(i); }
std::string core_label(std::size_t i) { return "Core " + std::to_string(i); }
}  // namespace

void CpuLoadProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // 使用率百分比：單位 "%"、值域有界 [0,100]（可正規化到 [0,1] 供長條 / 直方圖）。
    metric_ = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, kUnit, ds::metrics::MetricRange::bounded(0.0, 100.0));

    // 總體實例恆存在（列舉順序第一）：instance_id="total"、label="CPU Total"。
    inst_total_ = &metric_->add_instance(kInstanceTotal, kTotalLabel, history_);

    // 以目前一份使用率填初值，並依其核心數建立每核心實例（初建亦把有效值推入歷史，
    // 與後續採集路徑一致）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(metric_);
}

void CpuLoadProvider::sample() {
    if (!metric_) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

void CpuLoadProvider::write_ratio(ds::metrics::InMemoryMetricInstance* inst, double ratio,
                                  bool valid, bool to_history) {
    using ds::metrics::MetricValue;
    if (!valid) {
        // 無讀值：設為未知且**不**推入歷史（不污染序列），保守不謊報 0。
        inst->set_value(MetricValue::unknown());
        return;
    }
    const MetricValue v = MetricValue::of(ratio * kPercentScale);  // 比率 [0,1] → %
    if (to_history) {
        inst->update(v);  // 推入歷史（update 於 valid 值才推）
    } else {
        inst->set_value(v);
    }
}

void CpuLoadProvider::apply(const CpuUsageSample& usage, bool to_history) {
    // 總體實例：valid 決定是否為未知。
    write_ratio(inst_total_, usage.overall, usage.valid, to_history);

    const std::size_t n = usage.valid ? usage.per_core.size() : 0;

    // 必要時動態擴增核心實例（新取樣核心數多於既有）——unique_ptr 持有，既有參照不失效。
    for (std::size_t i = core_insts_.size(); i < n; ++i) {
        core_insts_.push_back(&metric_->add_instance(core_id(i), core_label(i), history_));
    }

    // 逐核寫值。
    for (std::size_t i = 0; i < core_insts_.size(); ++i) {
        if (i < n) {
            write_ratio(core_insts_[i], usage.per_core[i], /*valid=*/true, to_history);
        } else {
            // 本次取樣少了這顆核心（下線 / 無讀值）→ 設未知、不推歷史。
            write_ratio(core_insts_[i], 0.0, /*valid=*/false, to_history);
        }
    }
}

}  // namespace ds::sysinfo
