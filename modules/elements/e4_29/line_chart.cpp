// E4-29 折線圖 — 渲染描述模型實作（見 line_chart.hpp 規格）。
#include "line_chart.hpp"

#include <cmath>  // std::isfinite

namespace ds::elements {

namespace {

// 把值域 [min,max] 上的資料值正規化為幅值 [0,1]，並回報是否被夾限（超範圍）。
// 與 E4-17 histogram.cpp 的 normalize() 同邏輯（各單元自持一份，不共用內部細節）。
float normalize(double value, const LineChartRange& r, bool& clamped) {
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

// 簡單移動平均（窗口 3，邊界以可得鄰居平均，不做零填充——避免邊界失真）。
std::vector<double> moving_average(const std::vector<double>& v) {
    const std::size_t n = v.size();
    std::vector<double> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t lo = (i == 0) ? 0 : i - 1;
        const std::size_t hi = (i + 1 < n) ? i + 1 : n - 1;
        double sum = 0.0;
        std::size_t cnt = 0;
        for (std::size_t j = lo; j <= hi; ++j) {
            sum += v[j];
            ++cnt;
        }
        out[i] = sum / static_cast<double>(cnt);
    }
    return out;
}

}  // namespace

LineChartElement::LineChartElement() {
    series_.push_back(SeriesData{"", {}});  // series_[0]：預設序列，名稱 ""。
}

LineChartElement::SeriesData* LineChartElement::find_series(const std::string& name) {
    for (SeriesData& s : series_) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

const LineChartElement::SeriesData* LineChartElement::find_series(const std::string& name) const {
    for (const SeriesData& s : series_) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

void LineChartElement::trim_series_to_capacity(SeriesData& s) {
    if (capacity_ == kLineChartUnbounded) return;
    if (s.samples.size() <= capacity_) return;
    // 滾動視窗：只保留最新 N 筆（丟棄最舊者）。
    const std::size_t drop = s.samples.size() - capacity_;
    s.samples.erase(s.samples.begin(), s.samples.begin() + static_cast<std::ptrdiff_t>(drop));
}

LineChartStatus LineChartElement::set_range(double min, double max) {
    if (!std::isfinite(min) || !std::isfinite(max)) return LineChartStatus::Invalid;
    if (!(min < max)) return LineChartStatus::Invalid;  // 需嚴格 min<max（避免除以零 / 反轉）。
    range_.min = min;
    range_.max = max;
    return LineChartStatus::Ok;
}

LineChartStatus LineChartElement::set_capacity(std::size_t capacity) {
    capacity_ = capacity;
    for (SeriesData& s : series_) trim_series_to_capacity(s);
    return LineChartStatus::Ok;
}

LineChartStatus LineChartElement::set_composite(const ds::kernel::AlphaProfile& composite) {
    if (!std::isfinite(composite.opacity)) return LineChartStatus::Invalid;
    composite_ = composite;
    // opacity 為比例，夾限至 [0,1]（越界輸入永遠安全）。
    if (composite_.opacity < 0.0f) composite_.opacity = 0.0f;
    else if (composite_.opacity > 1.0f) composite_.opacity = 1.0f;
    return LineChartStatus::Ok;
}

LineChartStatus LineChartElement::push_sample(double v) {
    if (!std::isfinite(v)) return LineChartStatus::Invalid;
    series_[0].samples.push_back(v);
    trim_series_to_capacity(series_[0]);
    return LineChartStatus::Ok;
}

LineChartStatus LineChartElement::set_series(const std::vector<double>& values) {
    for (double v : values) {
        if (!std::isfinite(v)) return LineChartStatus::Invalid;  // 全有或全無，不部分寫入。
    }
    series_[0].samples = values;
    trim_series_to_capacity(series_[0]);
    return LineChartStatus::Ok;
}

void LineChartElement::clear() noexcept {
    for (SeriesData& s : series_) s.samples.clear();
}

LineChartStatus LineChartElement::add_series(const std::string& name) {
    if (name.empty()) return LineChartStatus::Invalid;      // "" 保留給預設序列。
    if (find_series(name) != nullptr) return LineChartStatus::Invalid;  // 重複名稱不靜默覆寫。
    series_.push_back(SeriesData{name, {}});
    return LineChartStatus::Ok;
}

LineChartStatus LineChartElement::push_sample(const std::string& name, double v) {
    if (!std::isfinite(v)) return LineChartStatus::Invalid;
    SeriesData* s = find_series(name);
    if (s == nullptr) return LineChartStatus::Invalid;  // 序列不存在，不靜默建立。
    s->samples.push_back(v);
    trim_series_to_capacity(*s);
    return LineChartStatus::Ok;
}

LineChartStatus LineChartElement::set_series(const std::string& name,
                                              const std::vector<double>& values) {
    SeriesData* s = find_series(name);
    if (s == nullptr) return LineChartStatus::Invalid;
    for (double v : values) {
        if (!std::isfinite(v)) return LineChartStatus::Invalid;  // 全有或全無。
    }
    s->samples = values;
    trim_series_to_capacity(*s);
    return LineChartStatus::Ok;
}

std::size_t LineChartElement::sample_count(const std::string& name) const {
    const SeriesData* s = find_series(name);
    return s == nullptr ? 0 : s->samples.size();
}

LineChartRenderModel LineChartElement::render_model() const {
    LineChartRenderModel model;
    model.range = range_;
    model.composite = composite_;
    model.smoothed = smoothing_enabled_;

    model.series.reserve(series_.size());
    bool any_nonempty = false;

    for (const SeriesData& s : series_) {
        LineChartSeriesModel sm;
        sm.name = s.name;
        sm.empty = s.samples.empty();
        sm.fill.enabled = fill_enabled_;
        sm.fill.baseline = 0.0f;  // 恆對應 range.min（圖表區底部之比例位置）。

        const std::size_t n = s.samples.size();
        std::vector<double> smoothed_values;
        if (smoothing_enabled_ && n > 0) smoothed_values = moving_average(s.samples);

        sm.points.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            LineChartPoint pt;
            pt.value = s.samples[i];
            pt.x = (n <= 1) ? 0.0f
                             : static_cast<float>(i) / static_cast<float>(n - 1);
            pt.y = normalize(s.samples[i], range_, pt.clamped);
            if (smoothing_enabled_) {
                bool smoothed_clamped = false;  // 平滑幅值獨立夾限，不覆寫原始點的 clamped 標記。
                pt.smoothed_y = normalize(smoothed_values[i], range_, smoothed_clamped);
            } else {
                pt.smoothed_y = pt.y;
            }
            sm.points.push_back(pt);
        }

        if (!sm.empty) any_nonempty = true;
        model.series.push_back(std::move(sm));
    }

    model.empty = !any_nonempty;
    return model;
}

}  // namespace ds::elements
