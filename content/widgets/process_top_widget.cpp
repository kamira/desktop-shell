// content/widgets/process_top_widget.cpp — C2-11 行程 top-N widget 實作
#include "process_top_widget.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace ds::widgets {

namespace {

using ds::format::EvalResult;
using ds::format::Evaluator;
using ds::format::Value;
using ds::format::VariableScope;
using ds::metrics::MetricRegistry;
using ds::sysinfo::ProcessSortKey;

// 本單元認得的宣告式欄位。不在此列的鍵一律視為錯誤（NFR-04 精神：拼錯的鍵不該被靜默忽略，
// 那會讓一份「其實沒生效」的設定看起來完全正常）。
bool is_known_field(const std::string& key) {
    return key == "sort_by" || key == "top_n" || key == "kind" || key == "min" || key == "max" ||
           key == "transform";
}

bool parse_kind(const std::string& s, ProcessRowElementKind& out) {
    if (s == "bar") {
        out = ProcessRowElementKind::Bar;
        return true;
    }
    if (s == "gauge") {
        out = ProcessRowElementKind::Gauge;
        return true;
    }
    if (s == "progress") {
        out = ProcessRowElementKind::Progress;
        return true;
    }
    if (s == "text") {
        out = ProcessRowElementKind::Text;
        return true;
    }
    return false;
}

// 格式化顯示值：固定一位小數 + 單位（"%" 緊接無空白；其餘單位以空白分隔；無單位不附加）。
// 與 C2-02 的呈現慣例一致。
std::string format_value(double v, const std::string& unit) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    std::string s(buf);
    if (unit.empty()) {
        return s;
    }
    if (unit == "%") {
        s += unit;
    } else {
        s += " ";
        s += unit;
    }
    return s;
}

// instance_id（E2-19 以 pid 字串為之）的排序次鍵：可解析為數字時以數值比較，否則退回字典序
// （以 (parsed, numeric) 配對表達，避免「無法解析」與「pid 0」撞號）。
std::pair<bool, unsigned long long> pid_key(const std::string& instance_id) {
    if (instance_id.empty()) {
        return {false, 0ULL};
    }
    char* end = nullptr;
    const unsigned long long n = std::strtoull(instance_id.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return {false, 0ULL};
    }
    return {true, n};
}

// 排序：可用列優先；其次排序維度原始值由高到低；同值以 pid 遞增（決定性，與 E2-19
// top_processes() 的次序規則一致）。
bool row_precedes(const ProcessRow& a, const ProcessRow& b) {
    if (a.available != b.available) {
        return a.available;  // unavailable 列一律置後。
    }
    if (a.available && a.raw_value != b.raw_value) {
        return a.raw_value > b.raw_value;
    }
    const std::pair<bool, unsigned long long> ka = pid_key(a.instance_id);
    const std::pair<bool, unsigned long long> kb = pid_key(b.instance_id);
    if (ka.first && kb.first) {
        if (ka.second != kb.second) {
            return ka.second < kb.second;
        }
        return false;
    }
    if (ka.first != kb.first) {
        return ka.first;  // 可解析為 pid 者優先（決定性）。
    }
    return a.instance_id < b.instance_id;
}

}  // namespace

const char* to_string(ProcessRowElementKind k) noexcept {
    switch (k) {
        case ProcessRowElementKind::Bar:
            return "bar";
        case ProcessRowElementKind::Gauge:
            return "gauge";
        case ProcessRowElementKind::Progress:
            return "progress";
        case ProcessRowElementKind::Text:
            return "text";
    }
    return "bar";
}

const char* to_string(ProcessTopStatus s) noexcept {
    switch (s) {
        case ProcessTopStatus::Ok:
            return "Ok";
        case ProcessTopStatus::Invalid:
            return "Invalid";
        case ProcessTopStatus::FormulaError:
            return "FormulaError";
        case ProcessTopStatus::FormatError:
            return "FormatError";
    }
    return "Invalid";
}

const char* to_string(ProcessMetricState s) noexcept {
    switch (s) {
        case ProcessMetricState::Ok:
            return "Ok";
        case ProcessMetricState::NotConfigured:
            return "NotConfigured";
        case ProcessMetricState::Missing:
            return "Missing";
        case ProcessMetricState::UnitMismatch:
            return "UnitMismatch";
        case ProcessMetricState::NoInstances:
            return "NoInstances";
    }
    return "NotConfigured";
}

// ---------------------------------------------------------------------------
// 靜態：維度 ↔ 指標身分（CHG-20260829-05 Q-A：切換維度 = 換掉註冊的指標）
// ---------------------------------------------------------------------------

const std::vector<std::size_t>& ProcessTopWidget::allowed_top_n() {
    // 驗收明定的集合：3 / 5 / 10，0 = 全部。
    static const std::vector<std::size_t> kAllowed{kAllProcesses, 3, 5, 10};
    return kAllowed;
}

bool ProcessTopWidget::is_allowed_top_n(std::size_t n) noexcept {
    const std::vector<std::size_t>& allowed = allowed_top_n();
    return std::find(allowed.begin(), allowed.end(), n) != allowed.end();
}

ds::metrics::MetricId ProcessTopWidget::metric_id_for(ProcessSortKey key) {
    // 直接取 E2-19 提供者的常數——不在這裡重打一份字串字面，否則上游改名時本單元會
    // 安靜地去要一個不存在的 id。
    return key == ProcessSortKey::Cpu
               ? ds::metrics::MetricId(ds::sysinfo::ProcessMonitorProvider::kCpuMetricId)
               : ds::metrics::MetricId(ds::sysinfo::ProcessMonitorProvider::kMemoryMetricId);
}

const char* ProcessTopWidget::unit_for(ProcessSortKey key) noexcept {
    return key == ProcessSortKey::Cpu ? ds::sysinfo::ProcessMonitorProvider::kCpuUnit
                                       : ds::sysinfo::ProcessMonitorProvider::kMemoryUnit;
}

bool ProcessTopWidget::parse_sort_key(const std::string& s, ProcessSortKey& out) {
    if (s == "cpu") {
        out = ProcessSortKey::Cpu;
        return true;
    }
    if (s == "memory") {
        out = ProcessSortKey::Memory;
        return true;
    }
    return false;
}

ProcessTopWidget::ProcessTopWidget(std::string id) : id_(std::move(id)) {}

// ---------------------------------------------------------------------------
// 設定
// ---------------------------------------------------------------------------

ProcessTopStatus ProcessTopWidget::parse_spec(const Value& definition, ProcessTopSpec& out) {
    if (!definition.is_map()) {
        last_error_ = "definition is not a map";
        return ProcessTopStatus::Invalid;
    }

    for (const Value::Member& m : definition.as_map()) {
        if (!is_known_field(m.first)) {
            last_error_ = "unknown field '" + m.first + "'";
            return ProcessTopStatus::Invalid;
        }
    }

    // --- sort_by（必填）：決定要讀哪一個指標 id 與期望單位 ---
    const Value* sort_field = definition.find("sort_by");
    if (sort_field == nullptr || !sort_field->is_string() ||
        !parse_sort_key(sort_field->as_string(), out.sort_key)) {
        last_error_ = "'sort_by' must be the string \"cpu\" or \"memory\"";
        return ProcessTopStatus::Invalid;
    }

    // --- top_n（必填）：僅允許 0 / 3 / 5 / 10 ---
    const Value* top_field = definition.find("top_n");
    if (top_field == nullptr || !top_field->is_number()) {
        last_error_ = "'top_n' must be a number (integral; 0 = all)";
        return ProcessTopStatus::Invalid;
    }
    // 不綁 E7-01 的「是否整數」旗標（同一個 3 由文字解析或由呼叫端建構可能帶不同旗標），
    // 而是看值本身是不是非負整數——判的是語意，不是來源。
    const double raw_n = top_field->as_number();
    if (!std::isfinite(raw_n) || std::floor(raw_n) != raw_n || raw_n < 0.0) {
        last_error_ = "'top_n' must be a non-negative integral number";
        return ProcessTopStatus::Invalid;
    }
    const std::size_t n = static_cast<std::size_t>(raw_n);
    if (!is_allowed_top_n(n)) {
        last_error_ = "'top_n' must be one of 0 (all) / 3 / 5 / 10";
        return ProcessTopStatus::Invalid;
    }
    out.top_n = n;

    // --- kind（選填）---
    if (const Value* kind = definition.find("kind")) {
        if (!kind->is_string() || !parse_kind(kind->as_string(), out.kind)) {
            last_error_ = "'kind' is not a recognized element kind";
            return ProcessTopStatus::Invalid;
        }
    }

    // --- min/max（選填，須成對；數字或 E7-05 靜態公式）---
    const Value* min_field = definition.find("min");
    const Value* max_field = definition.find("max");
    if ((min_field == nullptr) != (max_field == nullptr)) {
        last_error_ = "'min' and 'max' must be provided together";
        return ProcessTopStatus::Invalid;
    }
    if (min_field != nullptr && max_field != nullptr) {
        // 靜態公式：求值作用域**不含** `value`（引用它即為「未定義變數」→ FormulaError），
        // 因為顯示範圍不得隨當次指標值變動。
        auto resolve_bound = [this](const Value& field, double& value, bool& formula_failed) -> bool {
            if (field.is_number()) {
                value = field.as_number();
                return true;
            }
            if (field.is_string() && ds::format::is_formula(field.as_string())) {
                Evaluator ev;
                EvalResult r = ev.evaluate(field.as_string());
                if (!r.ok()) {
                    last_error_ = "'min'/'max' formula failed at column " +
                                  std::to_string(r.error().position) + ": " + r.error().message;
                    formula_failed = true;
                    return false;
                }
                if (!r.value().is_number()) {
                    last_error_ = "'min'/'max' formula did not evaluate to a number";
                    formula_failed = true;
                    return false;
                }
                value = r.value().as_number();
                return true;
            }
            return false;
        };

        double lo = 0.0;
        double hi = 0.0;
        bool formula_failed = false;
        if (!resolve_bound(*min_field, lo, formula_failed) ||
            !resolve_bound(*max_field, hi, formula_failed)) {
            if (formula_failed) {
                return ProcessTopStatus::FormulaError;
            }
            last_error_ = "'min'/'max' must be a number or an E7-05 formula string";
            return ProcessTopStatus::Invalid;
        }
        if (!(std::isfinite(lo) && std::isfinite(hi) && hi > lo)) {
            last_error_ = "'min'/'max' resolved to a degenerate range";
            return ProcessTopStatus::Invalid;
        }
        out.has_range_override = true;
        out.range_min = lo;
        out.range_max = hi;
    }

    // --- transform（選填 E7-05 公式；求值延後至 refresh()，因 `value` 僅於讀值後可知）---
    if (const Value* transform = definition.find("transform")) {
        if (!transform->is_string() || !ds::format::is_formula(transform->as_string())) {
            last_error_ = "'transform' must be an E7-05 formula string (leading '=' or '${ }')";
            return ProcessTopStatus::Invalid;
        }
        out.transform = transform->as_string();
    }

    return ProcessTopStatus::Ok;
}

ProcessTopStatus ProcessTopWidget::configure(const Value& definition) {
    last_error_.clear();

    // 全有或全無：解析到暫存 spec，任一失敗不動既有設定。
    ProcessTopSpec parsed;
    const ProcessTopStatus st = parse_spec(definition, parsed);
    if (st != ProcessTopStatus::Ok) {
        return st;
    }

    spec_ = parsed;
    configured_ = true;

    // 歷史不保留：設定（尤其是**排序維度**）一改，先前讀到的值就不再屬於現在這個指標，
    // 一律丟棄；需重新 refresh() 才有資料（provider 由 host 重建，見檔首 Q-A 說明）。
    has_refreshed_ = false;
    state_ = ProcessMetricState::NotConfigured;
    clear_rows();
    return ProcessTopStatus::Ok;
}

ProcessTopStatus ProcessTopWidget::configure_text(const std::string& text) {
    last_error_.clear();

    // E7-01：解析即驗證（縮排 / 重複鍵 / 根層 format_version 可解析且相容），失敗帶行號。
    ds::format::ParseResult pr = ds::format::parse(text);
    if (!pr.ok()) {
        last_error_ = "E7-01 parse failed at line " + std::to_string(pr.error().line) + ": " +
                      pr.error().message;
        return ProcessTopStatus::FormatError;
    }
    // Document::root 已去除 format_version 本身，是純內容 Map。
    return configure(pr.document().root);
}

void ProcessTopWidget::clear_rows() {
    rows_.clear();
    model_ = ProcessTopRenderModel{};
    model_.metric_id = metric_id();
    model_.state = state_;
}

// ---------------------------------------------------------------------------
// 刷新
// ---------------------------------------------------------------------------

bool ProcessTopWidget::resolve_range(const ds::metrics::MetricRange& metric_range, double& lo,
                                      double& hi) const {
    if (spec_.has_range_override) {
        lo = spec_.range_min;
        hi = spec_.range_max;
        return true;
    }
    if (metric_range.is_bounded() && std::isfinite(*metric_range.min) &&
        std::isfinite(*metric_range.max) && *metric_range.max > *metric_range.min) {
        lo = *metric_range.min;
        hi = *metric_range.max;
        return true;
    }
    // 無上界（如 proc.memory 的 at_least(0)）且未宣告 min/max → 不給範圍（K-002：憑空的上界
    // 會把 8 GB 與 100 MB 畫成一樣滿）。
    return false;
}

ProcessTopStatus ProcessTopWidget::refresh(const MetricRegistry& source) {
    if (!configured_) {
        last_error_ = "refresh() before configure()";
        state_ = ProcessMetricState::NotConfigured;
        return ProcessTopStatus::Invalid;
    }

    last_error_.clear();
    const ds::metrics::MetricId wanted = metric_id();  // **由設定維度決定**，不看 registry 裡有什麼。

    // 任何非 Ok 的查核結果都把列清空（誠實的「沒有資料」，不沿用上一輪的舊值）。
    std::shared_ptr<ds::metrics::Metric> metric = source.get(wanted);
    if (!metric) {
        state_ = ProcessMetricState::Missing;
        last_error_ = "metric '" + wanted +
                      "' is not registered (sort dimension changed? the host rebuilds the "
                      "provider — C2-11 does not)";
        has_refreshed_ = true;
        clear_rows();
        return ProcessTopStatus::Ok;
    }

    const std::string unit = metric->unit();
    if (unit != std::string(expected_unit())) {
        // id 對、單位不對 → registry 裡那個不是 E2-19 為本維度掛的指標。不讀、不猜、不畫。
        state_ = ProcessMetricState::UnitMismatch;
        last_error_ = "metric '" + wanted + "' reports unit '" + unit + "' but this sort dimension "
                      "expects '" + expected_unit() + "'";
        has_refreshed_ = true;
        clear_rows();
        return ProcessTopStatus::Ok;
    }

    if (metric->instance_count() == 0) {
        state_ = ProcessMetricState::NoInstances;
        last_error_ = "metric '" + wanted + "' has no process instances";
        has_refreshed_ = true;
        clear_rows();
        return ProcessTopStatus::Ok;
    }

    double range_lo = 0.0;
    double range_hi = 1.0;
    const bool has_range = resolve_range(metric->range(), range_lo, range_hi);

    std::vector<ProcessRow> rows;
    rows.reserve(metric->instance_count());

    for (std::size_t i = 0; i < metric->instance_count(); ++i) {
        const ds::metrics::MetricInstance& inst = metric->instance(i);
        ProcessRow row;
        row.instance_id = inst.instance_id();  // E2-19：pid
        row.label = inst.label();              // E2-19：行程名

        // 刻意**不**讀 inst.history()：本 widget 不保留歷史（Q-A）。
        const ds::metrics::MetricValue v = inst.value();
        if (!v.valid || !std::isfinite(v.number)) {
            // NFR-03：無讀值 / 非有限 → 該列降級，不中止其餘列。
            rows.push_back(std::move(row));
            continue;
        }

        row.raw_value = v.number;
        row.unit = unit;

        double display = row.raw_value;
        if (!spec_.transform.empty()) {
            VariableScope scope;
            scope.define("value", Value::number(row.raw_value));
            Evaluator ev(scope);
            EvalResult r = ev.evaluate(spec_.transform);
            if (!r.ok() || !r.value().is_number() || !std::isfinite(r.value().as_number())) {
                // transform 求值失敗 → 該列降級（不中止其餘列）。原因留在 last_error_。
                if (!r.ok()) {
                    last_error_ = "'transform' failed for instance '" + row.instance_id +
                                  "' at column " + std::to_string(r.error().position) + ": " +
                                  r.error().message;
                } else {
                    last_error_ = "'transform' did not evaluate to a finite number for instance '" +
                                  row.instance_id + "'";
                }
                ProcessRow degraded;
                degraded.instance_id = row.instance_id;
                degraded.label = row.label;
                rows.push_back(std::move(degraded));
                continue;
            }
            display = r.value().as_number();
        }

        row.available = true;
        row.display_value = display;
        row.has_range = has_range;
        if (has_range) {
            row.range_min = range_lo;
            row.range_max = range_hi;
        }
        rows.push_back(std::move(row));
    }

    // 依排序維度值由高到低（決定性次序；unavailable 置後），再截斷為前 top_n（0 = 全部）。
    std::stable_sort(rows.begin(), rows.end(), row_precedes);
    if (spec_.top_n != kAllProcesses && rows.size() > spec_.top_n) {
        rows.resize(spec_.top_n);
    }

    rows_ = std::move(rows);
    state_ = ProcessMetricState::Ok;
    has_refreshed_ = true;

    model_ = ProcessTopRenderModel{};
    model_.metric_id = wanted;
    model_.unit = unit;
    model_.state = state_;
    return ProcessTopStatus::Ok;
}

// ---------------------------------------------------------------------------
// 呈現
// ---------------------------------------------------------------------------

ProcessTopStatus ProcessTopWidget::render() {
    if (!has_refreshed_) {
        last_error_ = "render() before refresh()";
        return ProcessTopStatus::Invalid;
    }

    ProcessTopRenderModel out;
    out.metric_id = metric_id();
    out.state = state_;
    out.unit = (state_ == ProcessMetricState::Ok) ? model_.unit : std::string{};
    out.rows.reserve(rows_.size());

    const std::string surface = id_ + ".rows";

    for (const ProcessRow& src : rows_) {
        ProcessRow row = src;

        if (!row.available) {
            row.display_text = "\xE2\x80\x94";  // '—' EM DASH：明確的「未知」佔位，不靜默消失。
            out.rows.push_back(std::move(row));
            continue;
        }

        row.display_text = format_value(row.display_value, row.unit);

        if (!row.has_range) {
            // 無顯示範圍 → 不掛 E4-03 元素（K-002：憑空的上界 = 看起來正常的假資料）。
            out.rows.push_back(std::move(row));
            continue;
        }

        switch (spec_.kind) {
            case ProcessRowElementKind::Bar: {
                ds::elements::BarElement be(row.range_min, row.range_max);
                be.set_value(row.display_value);
                be.set_surface(surface);
                be.set_slot(row.instance_id);
                be.set_label(row.display_text);
                row.element = be.render_model();
                break;
            }
            case ProcessRowElementKind::Gauge: {
                ds::elements::GaugeElement ge(row.range_min, row.range_max);
                ge.set_value(row.display_value);
                ge.set_surface(surface);
                ge.set_slot(row.instance_id);
                ge.set_label(row.display_text);
                row.element = ge.render_model();
                break;
            }
            case ProcessRowElementKind::Progress: {
                // E4-03 Progress 的範圍固定 [0,100]，故以「值於顯示範圍內的比例」換算百分比
                // ——這樣 Progress 對任何維度（% 或 MB）都表達同一件事：佔宣告範圍的幾成。
                const double span = row.range_max - row.range_min;
                double clamped = row.display_value;
                if (clamped < row.range_min) clamped = row.range_min;
                if (clamped > row.range_max) clamped = row.range_max;
                const double percent = (span > 0.0) ? (clamped - row.range_min) / span * 100.0 : 0.0;
                ds::elements::ProgressElement pe;
                pe.set_percent(percent);
                pe.set_surface(surface);
                pe.set_slot(row.instance_id);
                pe.set_label(row.display_text);
                row.element = pe.render_model();
                break;
            }
            case ProcessRowElementKind::Text:
                // 只顯示文字，element 維持預設（未綁定 surface）。
                break;
        }

        out.rows.push_back(std::move(row));
    }

    model_ = std::move(out);
    return ProcessTopStatus::Ok;
}

}  // namespace ds::widgets
