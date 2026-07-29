// E4-18 徑向線 / 指針 — 實作
//
// 純邏輯：值↔範圍映射、角度 / 幾何比例夾限、渲染描述產生、E7-03 宣告式設定驅動。
// 無平台分支、無真實繪圖 API。
#include "radial_pointer.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace ds::elements {

namespace {

double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

double clamp_to(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

}  // namespace

// ── RadialPointerElement ──

RadialPointerElement::RadialPointerElement() = default;

RadialPointerElement::RadialPointerElement(double min, double max) {
    set_range(min, max);
}

void RadialPointerElement::set_range(double min, double max) {
    if (!std::isfinite(min) || !std::isfinite(max) || !(min < max)) {
        throw std::invalid_argument(
            "RadialPointerElement::set_range: 範圍無效（界須為有限值且 min < max）");
    }
    min_ = min;
    max_ = max;
}

void RadialPointerElement::set_value(double v) {
    if (!std::isfinite(v)) {
        throw std::invalid_argument(
            "RadialPointerElement::set_value: 值須為有限數（非 NaN/Inf）");
    }
    raw_value_ = v;
}

double RadialPointerElement::value() const noexcept {
    return clamp_to(raw_value_, min_, max_);
}

double RadialPointerElement::ratio() const noexcept {
    const double span = max_ - min_;
    if (!(span > 0.0)) {
        return 0.0;  // 防禦：範圍恆由 set_range 保證有效，此處僅保底不除以零。
    }
    return (value() - min_) / span;
}

void RadialPointerElement::set_angle_span(double start_angle_degrees, double sweep_degrees) {
    if (!std::isfinite(start_angle_degrees) || !std::isfinite(sweep_degrees) ||
        sweep_degrees == 0.0) {
        throw std::invalid_argument(
            "RadialPointerElement::set_angle_span: 角度跨距無效（角度須有限且 sweep 不得為 0）");
    }
    start_angle_ = start_angle_degrees;
    sweep_ = sweep_degrees;
}

double RadialPointerElement::angle() const noexcept {
    return start_angle_ + ratio() * sweep_;
}

void RadialPointerElement::set_length_ratio(double ratio) {
    if (!std::isfinite(ratio)) {
        throw std::invalid_argument(
            "RadialPointerElement::set_length_ratio: 長度比例須為有限數（非 NaN/Inf）");
    }
    length_ratio_ = clamp01(ratio);
}

void RadialPointerElement::set_thickness_ratio(double ratio) {
    if (!std::isfinite(ratio)) {
        throw std::invalid_argument(
            "RadialPointerElement::set_thickness_ratio: 粗細比例須為有限數（非 NaN/Inf）");
    }
    thickness_ratio_ = clamp01(ratio);
}

RenderModel RadialPointerElement::render_model() const {
    RenderModel m;
    m.surface = surface_;
    m.slot = slot_;
    m.value_ratio = ratio();
    m.start_angle_degrees = start_angle_;
    m.sweep_degrees = sweep_;
    m.angle_degrees = angle();
    m.length_ratio = length_ratio_;
    m.thickness_ratio = thickness_ratio_;
    m.color = color_;
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

ConfigStatus load_radial_pointer_config(const ds::format::Value& config_root,
                                        RadialPointerElement& out, std::string& err) {
    ds::format::Value m;
    const ConfigStatus expanded = expand_config(config_root, m, err);
    if (expanded != ConfigStatus::Ok) return expanded;

    bool bad = false;

    // --- 範圍 + 值 ---
    double lo = out.range_min();
    double hi = out.range_max();
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

    // --- 弧幾何 ---
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
            out.set_angle_span(start, sweep);
        } catch (const std::invalid_argument& e) {
            err = e.what();
            return ConfigStatus::InvalidField;
        }
    }

    // --- 指針幾何比例 ---
    double length_ratio = out.length_ratio();
    if (read_number(m, "length_ratio", length_ratio, bad)) {
        try {
            out.set_length_ratio(length_ratio);
        } catch (const std::invalid_argument& e) {
            err = e.what();
            return ConfigStatus::InvalidField;
        }
    }
    if (bad) {
        err = "length_ratio 欄位須為數字";
        return ConfigStatus::InvalidField;
    }

    double thickness_ratio = out.thickness_ratio();
    if (read_number(m, "thickness_ratio", thickness_ratio, bad)) {
        try {
            out.set_thickness_ratio(thickness_ratio);
        } catch (const std::invalid_argument& e) {
            err = e.what();
            return ConfigStatus::InvalidField;
        }
    }
    if (bad) {
        err = "thickness_ratio 欄位須為數字";
        return ConfigStatus::InvalidField;
    }

    // --- 顏色 / 具名佈局 ---
    std::string s;
    if (read_string(m, "color", s, bad)) out.set_color(s);
    if (read_string(m, "surface", s, bad)) out.set_surface(s);
    if (read_string(m, "slot", s, bad)) out.set_slot(s);
    if (bad) {
        err = "color/surface/slot 欄位須為字串";
        return ConfigStatus::InvalidField;
    }

    return ConfigStatus::Ok;
}

}  // namespace ds::elements
