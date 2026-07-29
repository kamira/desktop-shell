// E4-03 長條 / 進度 / 量表 — 實作
//
// 純邏輯：值↔範圍映射、渲染描述產生、E7-03 宣告式設定驅動。無平台分支、無真實繪圖 API。
#include "bar_gauge.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ds::elements {

namespace {

// 把有限數格式化為精簡文字：整數值不帶小數；其餘以 6 位有效位並去尾零。
std::string format_number(double v) {
    if (std::isfinite(v) && std::floor(v) == v && std::fabs(v) < 1e15) {
        return std::to_string(static_cast<long long>(v));
    }
    std::ostringstream os;
    os << std::setprecision(6) << v;
    return os.str();
}

double clamp_to(double v, const Range& r) {
    if (v < r.min) return r.min;
    if (v > r.max) return r.max;
    return v;
}

}  // namespace

// ── Range ──

bool Range::valid() const noexcept {
    return std::isfinite(min) && std::isfinite(max) && min < max;
}

// ── RangedElement ──

RangedElement::RangedElement(double min, double max) {
    set_range(min, max);
}

void RangedElement::set_range(double min, double max) {
    Range r{min, max};
    if (!r.valid()) {
        throw std::invalid_argument(
            "RangedElement::set_range: 範圍無效（界須為有限值且 min < max）");
    }
    range_ = r;
}

void RangedElement::set_value(double v) {
    if (!std::isfinite(v)) {
        throw std::invalid_argument("RangedElement::set_value: 值須為有限數（非 NaN/Inf）");
    }
    raw_value_ = v;
}

double RangedElement::value() const noexcept {
    return clamp_to(raw_value_, range_);
}

double RangedElement::ratio() const noexcept {
    const double span = range_.max - range_.min;
    if (!(span > 0.0)) {
        return 0.0;  // 防禦：範圍恆由 set_range 保證有效，此處僅保底不除以零。
    }
    return (value() - range_.min) / span;
}

std::string RangedElement::label() const {
    if (!label_override_.empty()) {
        return label_override_;
    }
    return format_number(value());
}

void RangedElement::fill_common(RenderModel& m) const {
    m.surface = surface_;
    m.slot = slot_;
    m.orientation = orientation_;
    m.fill_ratio = ratio();
    m.fill_color = fill_color_;
    m.track_color = track_color_;
    m.show_label = show_label_;
    m.label = show_label_ ? label() : std::string{};
}

// ── BarElement ──

RenderModel BarElement::render_model() const {
    RenderModel m;
    m.kind = ElementKind::Bar;
    fill_common(m);
    m.has_angle = false;
    return m;
}

// ── ProgressElement ──

ProgressElement::ProgressElement() : RangedElement(0.0, 100.0) {}

void ProgressElement::set_percent(double percent) {
    set_value(percent);  // 非有限值於此擲例外；範圍固定 [0,100]，超界由 value() 夾限。
}

double ProgressElement::percent() const noexcept {
    return value();  // 已夾限至 [0,100]。
}

RenderModel ProgressElement::render_model() const {
    RenderModel m;
    m.kind = ElementKind::Progress;
    fill_common(m);
    m.has_angle = false;
    // 進度標籤預設呈現為百分比（呼叫端以 set_label 覆寫時，fill_common 已填入該覆寫值）。
    if (m.show_label && !has_label_override()) {
        m.label = format_number(percent()) + "%";
    }
    return m;
}

// ── GaugeElement ──

GaugeElement::GaugeElement() : RangedElement(0.0, 1.0) {}

GaugeElement::GaugeElement(double min, double max) : RangedElement(min, max) {}

void GaugeElement::set_arc(double start_angle_degrees, double sweep_degrees) {
    if (!std::isfinite(start_angle_degrees) || !std::isfinite(sweep_degrees) ||
        sweep_degrees == 0.0) {
        throw std::invalid_argument(
            "GaugeElement::set_arc: 弧無效（角度須有限且 sweep 不得為 0）");
    }
    start_angle_ = start_angle_degrees;
    sweep_ = sweep_degrees;
}

double GaugeElement::angle() const noexcept {
    return start_angle_ + ratio() * sweep_;
}

RenderModel GaugeElement::render_model() const {
    RenderModel m;
    m.kind = ElementKind::Gauge;
    fill_common(m);
    m.has_angle = true;
    m.start_angle_degrees = start_angle_;
    m.sweep_degrees = sweep_;
    m.angle_degrees = angle();
    return m;
}

// ── E7-03 宣告式設定驅動 ──

namespace {

// 由已展開的設定 Map 讀取一個純量欄位。回傳指標（不存在為 nullptr）。
const ds::format::Value* field(const ds::format::Value& map, const std::string& key) {
    return map.find(key);
}

// 讀取數字欄位（Number）。存在但非 Number → 設 bad=true。
bool read_number(const ds::format::Value& map, const std::string& key, double& out, bool& bad) {
    const ds::format::Value* v = field(map, key);
    if (v == nullptr) return false;
    if (!v->is_number()) {
        bad = true;
        return false;
    }
    out = v->as_number();
    return true;
}

// 讀取字串欄位（String）。存在但非 String → 設 bad=true。
bool read_string(const ds::format::Value& map, const std::string& key, std::string& out,
                 bool& bad) {
    const ds::format::Value* v = field(map, key);
    if (v == nullptr) return false;
    if (!v->is_string()) {
        bad = true;
        return false;
    }
    out = v->as_string();
    return true;
}

// 讀取布林欄位（Bool）。存在但非 Bool → 設 bad=true。
bool read_bool(const ds::format::Value& map, const std::string& key, bool& out, bool& bad) {
    const ds::format::Value* v = field(map, key);
    if (v == nullptr) return false;
    if (!v->is_bool()) {
        bad = true;
        return false;
    }
    out = v->as_bool();
    return true;
}

// 套用 min/max/value + 共用欄位（顏色 / surface / slot / label / show_label）到任一 RangedElement。
// 回傳 Ok 或 InvalidField（err 填因）。orientation 亦於此處理（若 want_orientation）。
ConfigStatus apply_common(const ds::format::Value& m, RangedElement& out, bool want_range,
                          bool want_orientation, std::string& err) {
    bool bad = false;

    if (want_range) {
        double lo = out.range().min;
        double hi = out.range().max;
        const bool has_lo = read_number(m, "min", lo, bad);
        const bool has_hi = read_number(m, "max", hi, bad);
        if (bad) {
            err = "min/max 欄位須為數字";
            return ConfigStatus::InvalidField;
        }
        if (has_lo || has_hi) {
            try {
                out.set_range(lo, hi);
            } catch (const std::invalid_argument& e) {
                err = e.what();
                return ConfigStatus::InvalidField;
            }
        }
        double val = out.raw_value();
        if (read_number(m, "value", val, bad)) {
            try {
                out.set_value(val);
            } catch (const std::invalid_argument& e) {
                err = e.what();
                return ConfigStatus::InvalidField;
            }
        }
        if (bad) {
            err = "value 欄位須為數字";
            return ConfigStatus::InvalidField;
        }
    }

    if (want_orientation) {
        std::string ori;
        if (read_string(m, "orientation", ori, bad)) {
            if (ori == "horizontal") {
                out.set_orientation(Orientation::Horizontal);
            } else if (ori == "vertical") {
                out.set_orientation(Orientation::Vertical);
            } else {
                err = "orientation 須為 \"horizontal\" 或 \"vertical\"";
                return ConfigStatus::InvalidField;
            }
        }
        if (bad) {
            err = "orientation 欄位須為字串";
            return ConfigStatus::InvalidField;
        }
    }

    std::string s;
    if (read_string(m, "fill_color", s, bad)) out.set_fill_color(s);
    if (read_string(m, "track_color", s, bad)) out.set_track_color(s);
    if (read_string(m, "surface", s, bad)) out.set_surface(s);
    if (read_string(m, "slot", s, bad)) out.set_slot(s);
    if (read_string(m, "label", s, bad)) out.set_label(s);
    if (bad) {
        err = "fill_color/track_color/surface/slot/label 欄位須為字串";
        return ConfigStatus::InvalidField;
    }

    bool show = out.show_label();
    if (read_bool(m, "show_label", show, bad)) out.set_show_label(show);
    if (bad) {
        err = "show_label 欄位須為布林";
        return ConfigStatus::InvalidField;
    }

    return ConfigStatus::Ok;
}

// 以 E7-03 展開設定根的變數段落，回傳展開後的 Value（成功）；失敗回報。
ConfigStatus expand_config(const ds::format::Value& config_root, ds::format::Value& out,
                           std::string& err) {
    if (!config_root.is_map()) {
        err = "設定根須為 Map";
        return ConfigStatus::NotAMap;
    }
    ds::format::ResolveResult r = ds::format::expand(config_root);
    if (!r.ok()) {
        err = r.error().message;
        if (!r.error().variable.empty()) {
            err += "（變數：" + r.error().variable + "）";
        }
        return ConfigStatus::ResolveError;
    }
    out = r.value();
    return ConfigStatus::Ok;
}

}  // namespace

ConfigStatus load_bar_config(const ds::format::Value& config_root, BarElement& out,
                             std::string& err) {
    ds::format::Value m;
    const ConfigStatus st = expand_config(config_root, m, err);
    if (st != ConfigStatus::Ok) return st;
    return apply_common(m, out, /*want_range=*/true, /*want_orientation=*/true, err);
}

ConfigStatus load_progress_config(const ds::format::Value& config_root, ProgressElement& out,
                                  std::string& err) {
    ds::format::Value m;
    const ConfigStatus st = expand_config(config_root, m, err);
    if (st != ConfigStatus::Ok) return st;

    // percent 欄位（進度專屬）：夾限至 [0,100]，非數字 → InvalidField。
    bool bad = false;
    double pct = out.percent();
    if (read_number(m, "percent", pct, bad)) {
        try {
            out.set_percent(pct);
        } catch (const std::invalid_argument& e) {
            err = e.what();
            return ConfigStatus::InvalidField;
        }
    }
    if (bad) {
        err = "percent 欄位須為數字";
        return ConfigStatus::InvalidField;
    }

    // 其餘共用欄位（不含 range / orientation：進度範圍固定 [0,100]）。
    return apply_common(m, out, /*want_range=*/false, /*want_orientation=*/true, err);
}

ConfigStatus load_gauge_config(const ds::format::Value& config_root, GaugeElement& out,
                               std::string& err) {
    ds::format::Value m;
    const ConfigStatus st = expand_config(config_root, m, err);
    if (st != ConfigStatus::Ok) return st;

    const ConfigStatus common =
        apply_common(m, out, /*want_range=*/true, /*want_orientation=*/false, err);
    if (common != ConfigStatus::Ok) return common;

    // 弧幾何（量表專屬）。
    bool bad = false;
    double start = out.start_angle();
    double sweep = out.sweep();
    const bool has_start = read_number(m, "start_angle", start, bad);
    const bool has_sweep = read_number(m, "sweep", sweep, bad);
    if (bad) {
        err = "start_angle/sweep 欄位須為數字";
        return ConfigStatus::InvalidField;
    }
    if (has_start || has_sweep) {
        try {
            out.set_arc(start, sweep);
        } catch (const std::invalid_argument& e) {
            err = e.what();
            return ConfigStatus::InvalidField;
        }
    }
    return ConfigStatus::Ok;
}

}  // namespace ds::elements
