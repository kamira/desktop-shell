// E2-27 螢幕像素取樣 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：以 E2-01 記憶體內實作把每個具名取樣 anchor 建成一個實例、掛上註冊表；
// 採集分級沿用 E2-02 的 SamplingTier / SamplingScheduler。
// 無 `#ifdef`、無系統呼叫、無真實螢幕擷取——換平台一行不動。
#include "screen_pixel.hpp"

#include <cstdio>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// PixelColor
// ---------------------------------------------------------------------------
std::string PixelColor::hex() const {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                  static_cast<unsigned>(r), static_cast<unsigned>(g),
                  static_cast<unsigned>(b));
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// ScreenAnchor 字串
// ---------------------------------------------------------------------------
const char* to_string(ScreenAnchor anchor) noexcept {
    switch (anchor) {
        case ScreenAnchor::Center:       return "center";
        case ScreenAnchor::TopLeft:      return "top-left";
        case ScreenAnchor::TopCenter:    return "top-center";
        case ScreenAnchor::TopRight:     return "top-right";
        case ScreenAnchor::CenterLeft:   return "center-left";
        case ScreenAnchor::CenterRight:  return "center-right";
        case ScreenAnchor::BottomLeft:   return "bottom-left";
        case ScreenAnchor::BottomCenter: return "bottom-center";
        case ScreenAnchor::BottomRight:  return "bottom-right";
    }
    return "center";  // 不可達；保守回中心
}

const char* to_label(ScreenAnchor anchor) noexcept {
    switch (anchor) {
        case ScreenAnchor::Center:       return "Center";
        case ScreenAnchor::TopLeft:      return "Top-Left";
        case ScreenAnchor::TopCenter:    return "Top-Center";
        case ScreenAnchor::TopRight:     return "Top-Right";
        case ScreenAnchor::CenterLeft:   return "Center-Left";
        case ScreenAnchor::CenterRight:  return "Center-Right";
        case ScreenAnchor::BottomLeft:   return "Bottom-Left";
        case ScreenAnchor::BottomCenter: return "Bottom-Center";
        case ScreenAnchor::BottomRight:  return "Bottom-Right";
    }
    return "Center";
}

// ---------------------------------------------------------------------------
// NullPixelSampleSource
// ---------------------------------------------------------------------------
void NullPixelSampleSource::set_pixel(ScreenAnchor anchor, PixelColor color) {
    for (auto& e : entries_) {
        if (e.first == anchor) {  // 覆寫既有
            e.second = color;
            return;
        }
    }
    entries_.emplace_back(anchor, color);
}

bool NullPixelSampleSource::clear(ScreenAnchor anchor) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->first == anchor) {
            entries_.erase(it);
            return true;
        }
    }
    return false;
}

void NullPixelSampleSource::clear_all() noexcept { entries_.clear(); }

std::optional<PixelColor> NullPixelSampleSource::sample(ScreenAnchor anchor) const {
    for (const auto& e : entries_) {
        if (e.first == anchor) return e.second;
    }
    return std::nullopt;  // 無讀值（Mac / null 期誠實預設）
}

// ---------------------------------------------------------------------------
// ScreenPixelProvider
// ---------------------------------------------------------------------------
namespace {
// 去重（保留首次出現順序）——避免同 instance_id 重複實例。
std::vector<ScreenAnchor> dedupe(const std::vector<ScreenAnchor>& in) {
    std::vector<ScreenAnchor> out;
    out.reserve(in.size());
    for (ScreenAnchor a : in) {
        bool seen = false;
        for (ScreenAnchor b : out) {
            if (a == b) { seen = true; break; }
        }
        if (!seen) out.push_back(a);
    }
    return out;
}
}  // namespace

ScreenPixelProvider::ScreenPixelProvider(std::shared_ptr<PixelSampleSource> source,
                                         std::vector<ScreenAnchor> anchors,
                                         ds::metrics::SamplingTier tier,
                                         std::size_t history_capacity)
    : source_(std::move(source)),
      anchors_(dedupe(anchors)),
      tier_(tier),
      history_capacity_(history_capacity) {}

ds::metrics::DemandId ScreenPixelProvider::register_demand(
    ds::metrics::SamplingScheduler& scheduler) const {
    // 沿用 E2-02：以本提供者的分級對排程器登記需求（除頻合併由排程器負責）。
    return scheduler.add_demand(kMetricId, tier_);
}

void ScreenPixelProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // 值 = 相對亮度正規化到 [0,1]，故 range = bounded(0,1)、unit = ""。
    auto metric = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, /*unit=*/"", ds::metrics::MetricRange::bounded(0.0, 1.0));

    for (ScreenAnchor anchor : anchors_) {
        // 每個具名取樣點 = 一個可列舉實例：
        //   instance_id = anchor 穩定字串（不含絕對座標）、label = 人類可讀。
        auto& inst = metric->add_instance(to_string(anchor), to_label(anchor),
                                          history_capacity_);

        // source 為 null 或無讀值 → 實例值為「未知」（保守，不崩、不污染歷史）。
        std::optional<PixelColor> px;
        if (source_) px = source_->sample(anchor);
        if (px) {
            // 有讀值：value.number = 相對亮度 [0,1]、text = 十六進位色碼（供取色）。
            //   用 update()：把亮度推入歷史環（環境光趨勢 / sparkline）。
            inst.update(ds::metrics::MetricValue::of(px->luminance(), px->hex()));
        } else {
            inst.set_value(ds::metrics::MetricValue::unknown());  // 不推歷史
        }
    }

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(std::move(metric));
}

}  // namespace ds::sysinfo
