// E1-16 螢幕邊緣熱區 — 實作
//
// 純資料註冊 + 用上游 E1-04 HitTester 做矩形幾何命中；不含任何平台分支、真實 OS / 繪圖 API。
#include "edge_hot_zone.hpp"

#include <cmath>  // std::isfinite
#include <utility>

namespace ds::kernel {

namespace {

bool is_corner(EdgeHotZone zone) {
    switch (zone) {
        case EdgeHotZone::TopLeft:
        case EdgeHotZone::TopRight:
        case EdgeHotZone::BottomLeft:
        case EdgeHotZone::BottomRight:
            return true;
        case EdgeHotZone::Left:
        case EdgeHotZone::Right:
        case EdgeHotZone::Top:
        case EdgeHotZone::Bottom:
            return false;
    }
    return false;  // 不可達（enum 已窮舉）
}

// 某熱區在給定螢幕尺寸下換算出的矩形範圍：本地原點 (x0, y0) 起、(width, height) 大小。
struct ZoneRect {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

ZoneRect zone_rect(EdgeHotZone zone, float ratio, float screen_w, float screen_h) {
    const float edge_w = screen_w * ratio;
    const float edge_h = screen_h * ratio;
    switch (zone) {
        case EdgeHotZone::Left:
            return ZoneRect{0.0f, 0.0f, edge_w, screen_h};
        case EdgeHotZone::Right:
            return ZoneRect{screen_w - edge_w, 0.0f, edge_w, screen_h};
        case EdgeHotZone::Top:
            return ZoneRect{0.0f, 0.0f, screen_w, edge_h};
        case EdgeHotZone::Bottom:
            return ZoneRect{0.0f, screen_h - edge_h, screen_w, edge_h};
        case EdgeHotZone::TopLeft:
            return ZoneRect{0.0f, 0.0f, edge_w, edge_h};
        case EdgeHotZone::TopRight:
            return ZoneRect{screen_w - edge_w, 0.0f, edge_w, edge_h};
        case EdgeHotZone::BottomLeft:
            return ZoneRect{0.0f, screen_h - edge_h, edge_w, edge_h};
        case EdgeHotZone::BottomRight:
            return ZoneRect{screen_w - edge_w, screen_h - edge_h, edge_w, edge_h};
    }
    return ZoneRect{};  // 不可達（enum 已窮舉）
}

bool screen_valid(const ScreenExtent& screen) {
    return std::isfinite(screen.width) && std::isfinite(screen.height) &&
           screen.width > 0.0f && screen.height > 0.0f;
}

bool point_valid(const LocalPoint& p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

}  // namespace

bool is_named_zone(EdgeHotZone zone) {
    switch (zone) {
        case EdgeHotZone::Left:
        case EdgeHotZone::Right:
        case EdgeHotZone::Top:
        case EdgeHotZone::Bottom:
        case EdgeHotZone::TopLeft:
        case EdgeHotZone::TopRight:
        case EdgeHotZone::BottomLeft:
        case EdgeHotZone::BottomRight:
            return true;
    }
    return false;  // 未知列舉值（如由外部 static_cast 產生）
}

HotZoneStatus EdgeHotZoneRegistry::register_zone(EdgeHotZone zone, float thickness_ratio,
                                                  HotZoneAction action) {
    if (!is_named_zone(zone)) {
        return HotZoneStatus::InvalidZone;  // 報錯不靜默：不註冊
    }
    if (!std::isfinite(thickness_ratio) || thickness_ratio <= 0.0f ||
        thickness_ratio > 1.0f) {
        return HotZoneStatus::InvalidThickness;  // 報錯不靜默：不註冊
    }
    zones_.push_back(Entry{zone, thickness_ratio, std::move(action)});
    return HotZoneStatus::Ok;
}

std::optional<TriggeredHotZone> EdgeHotZoneRegistry::test(const LocalPoint& point,
                                                           const ScreenExtent& screen) const {
    if (!point_valid(point) || !screen_valid(screen)) {
        return std::nullopt;  // 幾何前提不成立：無熱區可觸發
    }

    bool has_corner_hit = false;
    Entry corner_hit{};
    bool has_edge_hit = false;
    Entry edge_hit{};

    for (const auto& entry : zones_) {
        const ZoneRect r =
            zone_rect(entry.zone, entry.thickness_ratio, screen.width, screen.height);
        const Shape rect = make_rect(r.width, r.height);
        const LocalPoint local{point.x - r.x0, point.y - r.y0};
        const HitResult hr = tester_.hit_test(local, rect);
        if (hr.status != HitStatus::Ok || !hr.inside) {
            continue;
        }
        if (is_corner(entry.zone)) {
            has_corner_hit = true;
            corner_hit = entry;  // 同類別中，較晚註冊者覆蓋（後者為準）
        } else {
            has_edge_hit = true;
            edge_hit = entry;
        }
    }

    // 角落熱區優先於邊熱區。
    if (has_corner_hit) {
        return TriggeredHotZone{corner_hit.zone, corner_hit.action};
    }
    if (has_edge_hit) {
        return TriggeredHotZone{edge_hit.zone, edge_hit.action};
    }
    return std::nullopt;
}

std::size_t EdgeHotZoneRegistry::size() const { return zones_.size(); }

}  // namespace ds::kernel
