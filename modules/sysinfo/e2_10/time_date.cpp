// E2-10 時間與日期 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：以 Howard Hinnant 的 civil-from-days 純算術把 epoch 秒分解為 UTC 曆法欄位，
// 以 std::snprintf 格式化為固定寬度字串，再用 E2-01 記憶體內實作把三個面向掛成實例。
// 無 `#ifdef`、無系統呼叫、無真實後端——換平台一行不動。
#include "time_date.hpp"

#include <cstdio>

namespace ds::sysinfo {

namespace {

// 由 1970-01-01 起的曆日數還原為 (year, month, day)。
// 演算法：Howard Hinnant, "chrono-Compatible Low-Level Date Algorithms"（公有領域）。
// 純整數算術，對負曆日亦正確；無任何 OS / 時區相依。
void civil_from_days(std::int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;  // 位移使紀元對齊 0000-03-01
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);            // [0, 146096]
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
    const std::int64_t y0 = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);             // [0, 365]
    const unsigned mp = (5 * doy + 2) / 153;                                  // [0, 11]
    d = doy - (153 * mp + 2) / 5 + 1;                                         // [1, 31]
    m = mp < 10 ? mp + 3 : mp - 9;                                            // [1, 12]
    y = static_cast<int>(y0 + (m <= 2 ? 1 : 0));
}

}  // namespace

CivilTime civil_from_epoch_seconds(std::int64_t epoch_seconds) {
    CivilTime c;
    c.epoch_seconds = epoch_seconds;

    // Floor 除法：把秒切成「曆日數」與「日內秒 [0,86399]」，對負 epoch 亦正確。
    constexpr std::int64_t kSecsPerDay = 86400;
    std::int64_t days = epoch_seconds / kSecsPerDay;
    std::int64_t sod = epoch_seconds % kSecsPerDay;
    if (sod < 0) {
        sod += kSecsPerDay;
        days -= 1;
    }
    c.days_since_epoch = days;
    c.second_of_day = static_cast<int>(sod);

    civil_from_days(days, c.year, c.month, c.day);
    c.hour = static_cast<unsigned>(sod / 3600);
    c.minute = static_cast<unsigned>((sod % 3600) / 60);
    c.second = static_cast<unsigned>(sod % 60);
    return c;
}

CivilTime civil_from_time_point(std::chrono::system_clock::time_point tp) {
    const std::int64_t secs =
        std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
    return civil_from_epoch_seconds(secs);
}

std::string format_iso8601(const CivilTime& c) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02uT%02u:%02u:%02uZ", c.year, c.month, c.day,
                  c.hour, c.minute, c.second);
    return std::string(buf);
}

std::string format_date(const CivilTime& c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u", c.year, c.month, c.day);
    return std::string(buf);
}

std::string format_clock(const CivilTime& c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u", c.hour, c.minute, c.second);
    return std::string(buf);
}

void TimeDateProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // 時間軸無界；各面向數值維度異質，unit 留空、表述由各實例 text 承載。
    auto metric = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, /*unit=*/"", ds::metrics::MetricRange::unbounded());

    // 三個面向：epoch / date / time。時間點無時序歷史，故 history_capacity = 0。
    auto& epoch_inst = metric->add_instance(kInstanceEpoch, "Epoch", /*history_capacity=*/0);
    auto& date_inst = metric->add_instance(kInstanceDate, "Date", /*history_capacity=*/0);
    auto& time_inst = metric->add_instance(kInstanceTime, "Time", /*history_capacity=*/0);

    if (source_) {
        // 取樣注入的時間來源（決定性：測試餵固定值即得固定結果）。
        const CivilTime c = civil_from_epoch_seconds(source_->now_epoch_seconds());
        // 各面向：number = 對應數值維度、text = 人類可讀表述。
        // 用 set_value（不推歷史）——與 history_capacity 0 一致。
        epoch_inst.set_value(ds::metrics::MetricValue::of(
            static_cast<double>(c.epoch_seconds), format_iso8601(c)));
        date_inst.set_value(ds::metrics::MetricValue::of(
            static_cast<double>(c.days_since_epoch), format_date(c)));
        time_inst.set_value(ds::metrics::MetricValue::of(
            static_cast<double>(c.second_of_day), format_clock(c)));
    } else {
        // source 為 null → 三個實例皆「未知」，保守不崩、不謊報 0 為真實讀值。
        epoch_inst.set_value(ds::metrics::MetricValue::unknown());
        date_inst.set_value(ds::metrics::MetricValue::unknown());
        time_inst.set_value(ds::metrics::MetricValue::unknown());
    }

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(std::move(metric));
}

}  // namespace ds::sysinfo
