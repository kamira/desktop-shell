// E2-11 系統運行時間 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：由注入式來源讀運行時間、把秒數格式化為 "Nd HH:MM:SS"、以 E2-01 記憶體內實作
// 把 uptime（+ 可選開機時間戳）建成可列舉實例、掛上註冊表，並支援 sample() 重新讀取更新
// （uptime 推入歷史）。無 `#ifdef`、無系統呼叫、無真實時間 API（無 sysctl kern.boottime /
// /proc/uptime / GetTickCount / mach）——換平台一行不動。
#include "uptime.hpp"

#include <cstdio>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// format_uptime（自由函式，獨立可測；純算術）
// ---------------------------------------------------------------------------
std::string format_uptime(double seconds) {
    // 負值（理論不該發生，uptime 只增）視為 0；小數秒截斷取整。
    long long total = (seconds > 0.0) ? static_cast<long long>(seconds) : 0LL;
    const long long days = total / 86400;
    total %= 86400;
    const long long hours = total / 3600;
    total %= 3600;
    const long long mins = total / 60;
    const long long secs = total % 60;

    char buf[64];
    // "Nd HH:MM:SS"：天數不補零（可任意位數），時/分/秒補零到兩位。
    std::snprintf(buf, sizeof(buf), "%lldd %02lld:%02lld:%02lld", days, hours, mins, secs);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// NullUptimeSource
// ---------------------------------------------------------------------------
UptimeReading NullUptimeSource::read() {
    if (sequence_.empty()) {
        return fixed_;  // 無序列 → 回固定值（預設 unknown）
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx = (cursor_ < sequence_.size()) ? cursor_ : sequence_.size() - 1;
    if (cursor_ < sequence_.size()) ++cursor_;
    return sequence_[idx];
}

// ---------------------------------------------------------------------------
// UptimeProvider
// ---------------------------------------------------------------------------
void UptimeProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // uptime 秒數：單位 "s"、值域下界 0、上無界（uptime 只增，無自然上限）。
    metric_ = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, kUnit, ds::metrics::MetricRange::at_least(0.0));

    // uptime 實例恆存在（列舉順序第一）：instance_id="uptime"、label="System Uptime"、
    // 保留歷史環（配合 E2-02 週期採集鋪成序列）。
    inst_uptime_ = &metric_->add_instance(kInstanceUptime, kUptimeLabel, history_);

    // 以目前一份讀值填初值（有效值亦推入歷史，與後續採集路徑一致）；讀值帶開機時間戳時
    // 建立可選的 "boot.time" 實例。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(metric_);
}

void UptimeProvider::sample() {
    if (!metric_) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

void UptimeProvider::apply(const UptimeReading& r, bool to_history) {
    using ds::metrics::MetricValue;

    // uptime 實例：valid 決定是否為未知。
    if (!r.valid) {
        // 無讀值：設為未知且**不**推入歷史（不污染序列），保守不謊報 0。
        inst_uptime_->set_value(MetricValue::unknown());
    } else {
        // 有效：number = 秒數、text = "Nd HH:MM:SS" 格式化文字。
        const MetricValue v = MetricValue::of(r.seconds, format_uptime(r.seconds));
        if (to_history) {
            inst_uptime_->update(v);  // 推入歷史（update 於 valid 值才推）
        } else {
            inst_uptime_->set_value(v);
        }
    }

    // 可選開機時間戳實例：僅在讀值有效且帶開機時間戳時建立 / 更新。
    if (r.valid && r.boot_unix_time.has_value()) {
        if (!inst_boot_) {
            // 首次出現開機時間戳 → 動態新增實例（靜態值，history_capacity=0，無歷史）。
            // unique_ptr 持有實例，故既有 uptime 實例參照不失效。
            inst_boot_ = &metric_->add_instance(kInstanceBoot, kBootLabel,
                                                /*history_capacity=*/0);
        }
        // 靜態時間戳：只設值不動歷史（與 capacity 0 一致）。
        inst_boot_->set_value(MetricValue::of(*r.boot_unix_time));
    } else if (inst_boot_) {
        // 曾有開機時間戳、本次無 → 設未知（誠實表達本次無讀值，不縮減實例）。
        inst_boot_->set_value(MetricValue::unknown());
    }
}

}  // namespace ds::sysinfo
