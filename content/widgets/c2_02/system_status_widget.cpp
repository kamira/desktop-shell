// content/widgets/c2_02/system_status_widget.cpp — C2-02 系統狀態 widget 實作
#include "system_status_widget.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

namespace ds::widgets {

namespace {

using ds::format::EvalResult;
using ds::format::Evaluator;
using ds::format::ResolveError;
using ds::format::Value;
using ds::format::VariableScope;
using ds::metrics::MetricId;
using ds::metrics::MetricRegistry;
using ds::metrics::SamplingTier;

bool parse_kind(const std::string& s, MetricElementKind& out) {
    if (s == "bar") {
        out = MetricElementKind::Bar;
        return true;
    }
    if (s == "gauge") {
        out = MetricElementKind::Gauge;
        return true;
    }
    if (s == "progress") {
        out = MetricElementKind::Progress;
        return true;
    }
    if (s == "text") {
        out = MetricElementKind::Text;
        return true;
    }
    return false;
}

bool parse_tier(const std::string& s, SamplingTier& out) {
    if (s == "high") {
        out = SamplingTier::High;
        return true;
    }
    if (s == "normal") {
        out = SamplingTier::Normal;
        return true;
    }
    if (s == "low") {
        out = SamplingTier::Low;
        return true;
    }
    if (s == "on-demand") {
        out = SamplingTier::OnDemand;
        return true;
    }
    return false;
}

// 格式化顯示值：固定一位小數 + 單位（"%" 緊接無空白；其餘單位以空白分隔；無單位不附加）。
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

}  // namespace

const char* to_string(MetricElementKind k) noexcept {
    switch (k) {
        case MetricElementKind::Bar:
            return "bar";
        case MetricElementKind::Gauge:
            return "gauge";
        case MetricElementKind::Progress:
            return "progress";
        case MetricElementKind::Text:
            return "text";
    }
    return "bar";
}

const char* to_string(SystemStatusStatus s) noexcept {
    switch (s) {
        case SystemStatusStatus::Ok:
            return "Ok";
        case SystemStatusStatus::Invalid:
            return "Invalid";
        case SystemStatusStatus::ResolveError:
            return "ResolveError";
        case SystemStatusStatus::FormulaError:
            return "FormulaError";
    }
    return "Invalid";
}

SystemStatusWidget::SystemStatusWidget(std::string id, ds::kernel::KernelBackend& backend,
                                        ds::kernel::LayerStack& layers,
                                        const ds::render::FontMetrics& metrics)
    : id_(std::move(id)),
      base_(id_, backend, layers),
      metrics_(metrics),
      layout_(metrics_, id_ + ".text") {}

SystemStatusStatus SystemStatusWidget::parse_entry(const Value& item, const VariableScope& scope,
                                                    MetricEntrySpec& out, std::string& err) {
    if (!item.is_map()) {
        err = "metric entry is not a map";
        return SystemStatusStatus::Invalid;
    }

    const Value* id_field = item.find("id");
    if (id_field == nullptr || !id_field->is_string() || id_field->as_string().empty()) {
        err = "metric entry missing non-empty 'id'";
        return SystemStatusStatus::Invalid;
    }
    out.metric_id = id_field->as_string();

    if (const Value* label = item.find("label")) {
        if (!label->is_string()) {
            err = "'label' must be a string";
            return SystemStatusStatus::Invalid;
        }
        out.label = label->as_string();
    }

    if (const Value* kind = item.find("kind")) {
        if (!kind->is_string() || !parse_kind(kind->as_string(), out.kind)) {
            err = "'kind' is not a recognized element kind";
            return SystemStatusStatus::Invalid;
        }
    }

    const Value* min_field = item.find("min");
    const Value* max_field = item.find("max");
    if ((min_field == nullptr) != (max_field == nullptr)) {
        err = "'min' and 'max' must be provided together";
        return SystemStatusStatus::Invalid;
    }
    if (min_field != nullptr && max_field != nullptr) {
        auto resolve_bound = [&](const Value& field, double& value) -> bool {
            if (field.is_number()) {
                value = field.as_number();
                return true;
            }
            if (field.is_string() && ds::format::is_formula(field.as_string())) {
                Evaluator ev(scope);
                EvalResult r = ev.evaluate(field.as_string());
                if (!r.ok() || !r.value().is_number()) {
                    return false;
                }
                value = r.value().as_number();
                return true;
            }
            return false;
        };

        double lo = 0.0;
        double hi = 0.0;
        if (!resolve_bound(*min_field, lo) || !resolve_bound(*max_field, hi)) {
            err = "'min'/'max' must be a number or a valid E7-05 formula";
            return SystemStatusStatus::FormulaError;
        }
        if (!(std::isfinite(lo) && std::isfinite(hi) && hi > lo)) {
            err = "'min'/'max' resolved to a degenerate range";
            return SystemStatusStatus::Invalid;
        }
        out.has_range_override = true;
        out.range_min = lo;
        out.range_max = hi;
    }

    if (const Value* transform = item.find("transform")) {
        if (!transform->is_string() || !ds::format::is_formula(transform->as_string())) {
            err = "'transform' must be an E7-05 formula string";
            return SystemStatusStatus::Invalid;
        }
        out.transform = transform->as_string();
    }

    if (const Value* tier = item.find("tier")) {
        if (!tier->is_string() || !parse_tier(tier->as_string(), out.tier)) {
            err = "'tier' is not a recognized sampling tier";
            return SystemStatusStatus::Invalid;
        }
    }

    return SystemStatusStatus::Ok;
}

SystemStatusStatus SystemStatusWidget::configure(const Value& definition) {
    if (!definition.is_map()) {
        return SystemStatusStatus::Invalid;
    }

    // --- E7-03：由 vars: 段落建立本次設定的作用域（供 min/max 靜態公式與 transform 動態公式引用）---
    VariableScope new_vars;
    if (const Value* vars_section = definition.find("vars")) {
        ResolveError verr;
        if (!ds::format::build_scope(*vars_section, new_vars, verr)) {
            return SystemStatusStatus::ResolveError;
        }
    }

    const Value* metrics_field = definition.find("metrics");
    if (metrics_field == nullptr || !metrics_field->is_list()) {
        return SystemStatusStatus::Invalid;
    }

    // 全有或全無：以暫存作用域（new_vars）解析每筆項目；任一失敗則不動既有 vars_/entries_/
    // configured_（維持先前成功設定；若從未成功則維持未設定）。
    std::vector<MetricEntrySpec> parsed;
    parsed.reserve(metrics_field->as_list().size());
    for (const Value& item : metrics_field->as_list()) {
        MetricEntrySpec spec;
        std::string err;
        SystemStatusStatus st = parse_entry(item, new_vars, spec, err);
        if (st != SystemStatusStatus::Ok) {
            return st;
        }
        parsed.push_back(std::move(spec));
    }

    vars_ = std::move(new_vars);
    entries_ = std::move(parsed);
    configured_ = true;
    has_refreshed_ = false;
    last_refresh_.clear();
    model_ = SystemStatusRenderModel{};
    return SystemStatusStatus::Ok;
}

void SystemStatusWidget::release_demands() {
    if (demand_scheduler_ == nullptr) {
        return;
    }
    for (const DemandTicket& t : demand_tickets_) {
        demand_scheduler_->remove_demand(t.demand_id);
    }
    demand_tickets_.clear();
    demand_scheduler_ = nullptr;
}

void SystemStatusWidget::sync_demands(ds::metrics::SamplingScheduler& scheduler) {
    release_demands();
    if (!configured_ || entries_.empty()) {
        return;
    }
    demand_scheduler_ = &scheduler;
    demand_tickets_.reserve(entries_.size());
    for (const MetricEntrySpec& spec : entries_) {
        ds::metrics::DemandId id = scheduler.add_demand(spec.metric_id, spec.tier);
        demand_tickets_.push_back(DemandTicket{spec.metric_id, id});
    }
}

SystemStatusStatus SystemStatusWidget::refresh(const MetricRegistry& source) {
    if (!configured_) {
        return SystemStatusStatus::Invalid;
    }

    std::vector<MetricRenderEntry> results;
    results.reserve(entries_.size());

    for (const MetricEntrySpec& spec : entries_) {
        MetricRenderEntry e;
        e.metric_id = spec.metric_id;
        e.label = spec.label;

        std::shared_ptr<ds::metrics::Metric> metric = source.get(spec.metric_id);
        if (!metric || metric->instance_count() == 0) {
            // NFR-03：指標不存在 / 無實例 → 降級，略過本筆的其餘處理。
            results.push_back(std::move(e));
            continue;
        }

        const ds::metrics::MetricInstance& inst = metric->instance(0);
        const ds::metrics::MetricValue v = inst.value();
        if (!v.valid || !std::isfinite(v.number)) {
            results.push_back(std::move(e));
            continue;
        }

        e.available = true;
        e.raw_value = v.number;
        e.unit = metric->unit();
        if (e.label.empty()) {
            e.label = metric->name();
        }

        double display = e.raw_value;
        if (!spec.transform.empty()) {
            VariableScope value_scope(&vars_);
            value_scope.define("value", Value::number(e.raw_value));
            Evaluator ev(value_scope);
            EvalResult r = ev.evaluate(spec.transform);
            if (!r.ok() || !r.value().is_number() || !std::isfinite(r.value().as_number())) {
                // transform 求值失敗 → 該筆降級（NFR-03 精神：不因單一設定錯誤中止其餘指標）。
                MetricRenderEntry degraded;
                degraded.metric_id = spec.metric_id;
                degraded.label = e.label;
                results.push_back(std::move(degraded));
                continue;
            }
            display = r.value().as_number();
        }
        e.display_value = display;

        // 解出顯示範圍：宣告式覆寫 > Metric 有界 range() > 保守預設 [0,100]。
        if (spec.has_range_override) {
            e.range_min = spec.range_min;
            e.range_max = spec.range_max;
        } else {
            const ds::metrics::MetricRange mr = metric->range();
            if (mr.is_bounded() && *mr.max > *mr.min) {
                e.range_min = *mr.min;
                e.range_max = *mr.max;
            } else {
                e.range_min = 0.0;
                e.range_max = 100.0;
            }
        }

        results.push_back(std::move(e));
    }

    last_refresh_ = std::move(results);
    has_refreshed_ = true;
    return SystemStatusStatus::Ok;
}

SystemStatusStatus SystemStatusWidget::render() {
    if (!has_refreshed_) {
        return SystemStatusStatus::Invalid;
    }

    SystemStatusRenderModel out;
    out.entries.reserve(entries_.size());

    for (std::size_t i = 0; i < entries_.size() && i < last_refresh_.size(); ++i) {
        const MetricEntrySpec& spec = entries_[i];
        MetricRenderEntry entry = last_refresh_[i];

        std::string label_for_text = entry.label.empty() ? entry.metric_id : entry.label;

        if (!entry.available) {
            entry.display_text = "\xE2\x80\x94";  // '—' EM DASH：明確的「未知」佔位，不靜默消失。
            entry.text = layout_.layout(label_for_text + ": " + entry.display_text,
                                        ds::render::LayoutConstraints{});
            out.entries.push_back(std::move(entry));
            continue;
        }

        entry.display_text = format_value(entry.display_value, entry.unit);

        switch (spec.kind) {
            case MetricElementKind::Bar: {
                ds::elements::BarElement be(entry.range_min, entry.range_max);
                be.set_value(entry.display_value);
                be.set_surface(id_ + ".metrics");
                be.set_slot(entry.metric_id);
                be.set_show_label(false);
                entry.element = be.render_model();
                break;
            }
            case MetricElementKind::Gauge: {
                ds::elements::GaugeElement ge(entry.range_min, entry.range_max);
                ge.set_value(entry.display_value);
                ge.set_surface(id_ + ".metrics");
                ge.set_slot(entry.metric_id);
                ge.set_show_label(false);
                entry.element = ge.render_model();
                break;
            }
            case MetricElementKind::Progress: {
                ds::elements::ProgressElement pe;
                pe.set_percent(entry.display_value);
                pe.set_surface(id_ + ".metrics");
                pe.set_slot(entry.metric_id);
                pe.set_show_label(false);
                entry.element = pe.render_model();
                break;
            }
            case MetricElementKind::Text:
                // 只顯示文字，element 維持預設（未綁定 surface）。
                break;
        }

        entry.text = layout_.layout(label_for_text + ": " + entry.display_text,
                                    ds::render::LayoutConstraints{});
        out.entries.push_back(std::move(entry));
    }

    model_ = std::move(out);
    return SystemStatusStatus::Ok;
}

}  // namespace ds::widgets
