// E4-17 直方圖 — 渲染描述模型實作（見 histogram.hpp 規格）。
#include "histogram.hpp"

#include <cmath>    // std::isfinite
#include <cstdint>  // std::int64_t

namespace ds::elements {

namespace {

// 把值域 [min,max] 上的資料值正規化為幅值 [0,1]，並回報是否被夾限（超範圍）。
float normalize(double value, const HistogramRange& r, bool& clamped) {
    const double span = r.max - r.min;
    // 值域已於 set_range 驗證為 min<max 且有限，span > 0 恆成立。
    double t = (value - r.min) / span;
    clamped = false;
    if (t < 0.0) {
        t = 0.0;
        clamped = true;
    } else if (t > 1.0) {
        t = 1.0;
        clamped = true;
    }
    return static_cast<float>(t);
}

// 讀取 Value 為 double（Number 型別）。非 Number 回 false。
bool as_double(const ds::format::Value& v, double& out) {
    if (!v.is_number()) return false;
    out = v.as_number();
    return true;
}

}  // namespace

HistogramStatus HistogramElement::set_range(double min, double max) {
    if (!std::isfinite(min) || !std::isfinite(max)) return HistogramStatus::Invalid;
    if (!(min < max)) return HistogramStatus::Invalid;  // 需嚴格 min<max（避免除以零 / 反轉）。
    range_.min = min;
    range_.max = max;
    return HistogramStatus::Ok;
}

HistogramStatus HistogramElement::set_capacity(std::size_t capacity) {
    capacity_ = capacity;
    trim_to_capacity();
    return HistogramStatus::Ok;
}

HistogramStatus HistogramElement::set_threshold(double value) {
    if (!std::isfinite(value)) return HistogramStatus::Invalid;
    threshold_ = value;
    has_threshold_ = true;
    return HistogramStatus::Ok;
}

HistogramStatus HistogramElement::set_composite(const ds::kernel::AlphaProfile& composite) {
    if (!std::isfinite(composite.opacity)) return HistogramStatus::Invalid;
    composite_ = composite;
    // opacity 為比例，夾限至 [0,1]（越界輸入永遠安全）。
    if (composite_.opacity < 0.0f) composite_.opacity = 0.0f;
    else if (composite_.opacity > 1.0f) composite_.opacity = 1.0f;
    return HistogramStatus::Ok;
}

HistogramStatus HistogramElement::push_sample(double v) {
    if (!std::isfinite(v)) return HistogramStatus::Invalid;
    samples_.push_back(v);
    trim_to_capacity();
    return HistogramStatus::Ok;
}

HistogramStatus HistogramElement::set_series(const std::vector<double>& values) {
    // 先全數驗證再套用——任一非有限值即整批拒絕，不部分寫入（不靜默）。
    for (double v : values) {
        if (!std::isfinite(v)) return HistogramStatus::Invalid;
    }
    samples_ = values;
    trim_to_capacity();
    return HistogramStatus::Ok;
}

void HistogramElement::trim_to_capacity() {
    if (capacity_ == kHistogramUnbounded) return;
    if (samples_.size() <= capacity_) return;
    // 滾動視窗：只保留最新 N 筆（丟棄最舊者）。
    const std::size_t drop = samples_.size() - capacity_;
    samples_.erase(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(drop));
}

HistogramRenderModel HistogramElement::render_model() const {
    HistogramRenderModel model;
    model.range = range_;
    model.composite = composite_;
    model.empty = samples_.empty();

    model.bars.reserve(samples_.size());
    for (double v : samples_) {
        HistogramBar bar;
        bar.value = v;
        bar.magnitude = normalize(v, range_, bar.clamped);
        model.bars.push_back(bar);
    }

    if (has_threshold_) {
        model.threshold.present = true;
        model.threshold.value = threshold_;
        model.threshold.magnitude = normalize(threshold_, range_, model.threshold.clamped);
    }
    return model;
}

// ---------------------------------------------------------------------------
// 宣告式設定驅動
// ---------------------------------------------------------------------------

bool configure(const ds::format::Value& config, HistogramElement& out, HistogramConfigError& err) {
    if (!config.is_map()) {
        err.message = "histogram config: root must be a map";
        return false;
    }

    // range: { min, max }
    if (const ds::format::Value* range = config.find("range")) {
        if (!range->is_map()) {
            err.message = "histogram config: 'range' must be a map { min, max }";
            return false;
        }
        const ds::format::Value* min_v = range->find("min");
        const ds::format::Value* max_v = range->find("max");
        double min_d = 0.0, max_d = 0.0;
        if (!min_v || !max_v || !as_double(*min_v, min_d) || !as_double(*max_v, max_d)) {
            err.message = "histogram config: 'range' needs numeric 'min' and 'max'";
            return false;
        }
        if (out.set_range(min_d, max_d) != HistogramStatus::Ok) {
            err.message = "histogram config: invalid 'range' (need finite min < max)";
            return false;
        }
    }

    // capacity: int >= 0
    if (const ds::format::Value* cap = config.find("capacity")) {
        if (!cap->is_integer()) {
            err.message = "histogram config: 'capacity' must be an integer";
            return false;
        }
        const std::int64_t c = cap->as_int();
        if (c < 0) {
            err.message = "histogram config: 'capacity' must be >= 0";
            return false;
        }
        out.set_capacity(static_cast<std::size_t>(c));
    }

    // threshold: number
    if (const ds::format::Value* thr = config.find("threshold")) {
        double thr_d = 0.0;
        if (!as_double(*thr, thr_d)) {
            err.message = "histogram config: 'threshold' must be a number";
            return false;
        }
        if (out.set_threshold(thr_d) != HistogramStatus::Ok) {
            err.message = "histogram config: invalid 'threshold' (must be finite)";
            return false;
        }
    }

    // series: [ number, ... ]
    if (const ds::format::Value* series = config.find("series")) {
        if (!series->is_list()) {
            err.message = "histogram config: 'series' must be a list of numbers";
            return false;
        }
        std::vector<double> values;
        values.reserve(series->as_list().size());
        for (const ds::format::Value& item : series->as_list()) {
            double d = 0.0;
            if (!as_double(item, d)) {
                err.message = "histogram config: 'series' must contain only numbers";
                return false;
            }
            values.push_back(d);
        }
        if (out.set_series(values) != HistogramStatus::Ok) {
            err.message = "histogram config: 'series' contains a non-finite value";
            return false;
        }
    }

    return true;
}

bool configure_from_document(const ds::format::Document& doc, HistogramElement& out,
                             HistogramConfigError& err) {
    // 先以 E7-03 展開段落變數（vars: 段落 + ${...} 引用）。
    ds::format::DocumentResolveResult expanded = ds::format::expand(doc);
    if (!expanded) {
        const ds::format::ResolveError& e = expanded.error();
        err.message = "histogram config: variable expansion failed: " + e.message;
        if (!e.variable.empty()) err.message += " (variable: " + e.variable + ")";
        return false;
    }
    return configure(expanded.document().root, out, err);
}

}  // namespace ds::elements
