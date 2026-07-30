// E2-15 音訊峰值與頻譜 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：由 PCM 樣本算峰值 / RMS、線性→分貝、以 E2-01 記憶體內實作把 peak / rms 單一實例
// + 每頻段可列舉實例掛上註冊表，並支援 sample() 重新讀取更新（電平推入歷史）。
// 無 `#ifdef`、無系統呼叫、無真實音訊 API（無 CoreAudio / AudioUnit / WASAPI）
// ——換平台一行不動。
#include "audio_levels.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ds::sysinfo {

namespace {

// 夾到 [0,1]（電平 / 能量的合法域）。
double clamp01(double v) noexcept {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

// 生成第 i 頻段的穩定 instance_id（"band0" / "band1" / …）與 label（"Band 0" / …）。
std::string band_id(std::size_t i) { return "band" + std::to_string(i); }
std::string band_label(std::size_t i) { return "Band " + std::to_string(i); }

}  // namespace

// ---------------------------------------------------------------------------
// 電平計算（自由函式，獨立可測）
// ---------------------------------------------------------------------------
double peak_of(const std::vector<double>& pcm) noexcept {
    double m = 0.0;
    for (double x : pcm) {
        const double a = std::fabs(x);
        if (a > m) m = a;
    }
    return clamp01(m);
}

double rms_of(const std::vector<double>& pcm) noexcept {
    if (pcm.empty()) return 0.0;
    double sum_sq = 0.0;
    for (double x : pcm) sum_sq += x * x;
    return clamp01(std::sqrt(sum_sq / static_cast<double>(pcm.size())));
}

double linear_to_db(double amplitude, double floor_db) noexcept {
    const double a = std::fabs(amplitude);
    if (a <= 0.0) return floor_db;  // 靜音：以下限表達（避免 -∞）
    const double db = 20.0 * std::log10(a);
    return db < floor_db ? floor_db : db;  // 極小值夾到下限
}

// ---------------------------------------------------------------------------
// NullAudioLevelSource
// ---------------------------------------------------------------------------
void NullAudioLevelSource::set_pcm(const std::vector<double>& pcm, std::vector<double> bands) {
    AudioLevelSample s;
    s.valid = true;
    s.peak = peak_of(pcm);
    s.rms = rms_of(pcm);
    for (double& b : bands) b = clamp01(b);  // 頻段能量夾到合法域
    s.bands = std::move(bands);
    set_sample(std::move(s));
}

AudioLevelSample NullAudioLevelSource::sample() {
    if (sequence_.empty()) {
        return AudioLevelSample::unknown();  // 空列 → 無讀值（誠實）
    }
    // 列盡則持續回最後一份（穩定，不走出界）。
    const std::size_t idx = std::min(cursor_, sequence_.size() - 1);
    if (cursor_ < sequence_.size()) ++cursor_;
    return sequence_[idx];
}

// ---------------------------------------------------------------------------
// AudioLevelProvider
// ---------------------------------------------------------------------------
void AudioLevelProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    using ds::metrics::InMemoryMetric;
    using ds::metrics::MetricRange;

    // 沿用 E2-01 記憶體內實作，不自造指標模型。電平 / 能量皆線性 [0,1]，range 有界
    // （可正規化到 [0,1] 供長條 / 等化器類元件）。
    const MetricRange unit01 = MetricRange::bounded(0.0, 1.0);

    peak_metric_ = std::make_shared<InMemoryMetric>(kPeakMetricId, kPeakMetricName, kUnit, unit01);
    rms_metric_ = std::make_shared<InMemoryMetric>(kRmsMetricId, kRmsMetricName, kUnit, unit01);
    spectrum_metric_ =
        std::make_shared<InMemoryMetric>(kSpectrumMetricId, kSpectrumMetricName, kUnit, unit01);

    // 單一實例：peak / rms 各恰一個實例。
    peak_inst_ = &peak_metric_->add_instance(kPeakInstance, kPeakLabel, history_);
    rms_inst_ = &rms_metric_->add_instance(kRmsInstance, kRmsLabel, history_);

    // 以目前一份電平填初值，並依其頻段數建立每頻段實例（初建亦把有效值推入歷史，與後續
    // 採集路徑一致）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表（三個指標）；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(peak_metric_);
    registry.register_metric(rms_metric_);
    registry.register_metric(spectrum_metric_);
}

void AudioLevelProvider::sample() {
    if (!peak_metric_) return;  // 尚未 register_metrics：無指標可更新
    apply(current(), /*to_history=*/true);
}

void AudioLevelProvider::write_level(ds::metrics::InMemoryMetricInstance* inst, double amplitude,
                                     bool valid, bool to_history) {
    using ds::metrics::MetricValue;
    if (!valid) {
        // 無讀值：設為未知且**不**推入歷史（不污染序列），保守不謊報 0。
        inst->set_value(MetricValue::unknown());
        return;
    }
    const double linear = clamp01(amplitude);
    // number = 線性 [0,1]（供繪圖 / 正規化）；text = dB 表述（如 "-6.0 dB"）。
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f dB", linear_to_db(linear));
    const MetricValue v = MetricValue::of(linear, std::string(buf));
    if (to_history) {
        inst->update(v);  // 推入歷史（update 於 valid 值才推線性維度）
    } else {
        inst->set_value(v);
    }
}

void AudioLevelProvider::write_band(ds::metrics::InMemoryMetricInstance* inst, double energy,
                                    bool valid, bool to_history) {
    using ds::metrics::MetricValue;
    if (!valid) {
        inst->set_value(MetricValue::unknown());
        return;
    }
    const MetricValue v = MetricValue::of(clamp01(energy));  // 頻段能量無 dB 文字
    if (to_history) {
        inst->update(v);
    } else {
        inst->set_value(v);
    }
}

void AudioLevelProvider::apply(const AudioLevelSample& lvl, bool to_history) {
    // peak / rms 單一實例：valid 決定是否為未知。
    write_level(peak_inst_, lvl.peak, lvl.valid, to_history);
    write_level(rms_inst_, lvl.rms, lvl.valid, to_history);

    const std::size_t n = lvl.valid ? lvl.bands.size() : 0;

    // 必要時動態擴增頻段實例（新取樣頻段數多於既有）——unique_ptr 持有，既有參照不失效。
    for (std::size_t i = band_insts_.size(); i < n; ++i) {
        band_insts_.push_back(&spectrum_metric_->add_instance(band_id(i), band_label(i), history_));
    }

    // 逐頻段寫值。
    for (std::size_t i = 0; i < band_insts_.size(); ++i) {
        if (i < n) {
            write_band(band_insts_[i], lvl.bands[i], /*valid=*/true, to_history);
        } else {
            // 本次取樣少了這個頻段（或無讀值）→ 設未知、不推歷史。
            write_band(band_insts_[i], 0.0, /*valid=*/false, to_history);
        }
    }
}

}  // namespace ds::sysinfo
