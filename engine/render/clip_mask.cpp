// E4-23 容器裁切與遮罩 — 實作（平台中立宣告式狀態，見 clip_mask.hpp）
#include "clip_mask.hpp"

#include <algorithm>  // std::clamp
#include <cmath>      // std::isfinite

namespace ds::render {

namespace {

// 有限值檢查（NaN / Inf 一律視為無效，不進一步計算）。
bool finite(float v) { return std::isfinite(v); }

// [0,1] 內（含端點）且有限。
bool in_unit_range(float v) { return finite(v) && v >= 0.0f && v <= 1.0f; }

bool is_valid_norm_rect(const NormRect& r) {
    return finite(r.x0) && finite(r.y0) && finite(r.x1) && finite(r.y1) &&
           r.x0 >= 0.0f && r.x1 <= 1.0f && r.x0 < r.x1 &&
           r.y0 >= 0.0f && r.y1 <= 1.0f && r.y0 < r.y1;
}

// 圓角矩形命中測試：point 已知落在 rect 內（呼叫端先做 bounding-box 檢查）。
// radius_fraction 為容器短邊的比例（[0,0.5]），換算為正規化絕對半徑後判定。
bool point_in_rounded_rect(const NormRect& r, float radius_fraction, const NormPoint& p) {
    const float width = r.x1 - r.x0;
    const float height = r.y1 - r.y0;
    const float radius = radius_fraction * std::min(width, height);
    if (radius <= 0.0f) {
        return true;  // 退化為一般矩形：已在 bounding-box 內即算命中
    }
    const float left = r.x0 + radius;
    const float right = r.x1 - radius;
    const float top = r.y0 + radius;
    const float bottom = r.y1 - radius;
    // 中央十字帶（垂直 / 水平）內必為命中，不需檢查圓角。
    if (p.x >= left && p.x <= right) {
        return true;
    }
    if (p.y >= top && p.y <= bottom) {
        return true;
    }
    // 落在四個角落象限：檢查與最近角圓心的距離。
    const float cx = (p.x < left) ? left : right;
    const float cy = (p.y < top) ? top : bottom;
    const float dx = p.x - cx;
    const float dy = p.y - cy;
    return (dx * dx + dy * dy) <= (radius * radius);
}

// 點在多邊形內判定（ray casting；`path` 隱含首尾相連的封閉多邊形）。
bool point_in_polygon(const std::vector<NormPoint>& path, const NormPoint& p) {
    bool inside = false;
    const std::size_t n = path.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const NormPoint& a = path[i];
        const NormPoint& b = path[j];
        const bool straddles = (a.y > p.y) != (b.y > p.y);
        if (straddles) {
            const float x_at_p_y = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
            if (p.x < x_at_p_y) {
                inside = !inside;
            }
        }
    }
    return inside;
}

}  // namespace

// --- 純函式幾何工具 ---
bool is_valid_clip_region(const ClipRegion& region) {
    switch (region.shape) {
        case ClipShape::Rect:
            return is_valid_norm_rect(region.rect);
        case ClipShape::RoundedRect:
            return is_valid_norm_rect(region.rect) && finite(region.corner_radius) &&
                   region.corner_radius >= 0.0f && region.corner_radius <= 0.5f;
        case ClipShape::Path: {
            if (region.path.size() < 3) {
                return false;  // 至少三點才能構成多邊形
            }
            for (const NormPoint& pt : region.path) {
                if (!in_unit_range(pt.x) || !in_unit_range(pt.y)) {
                    return false;
                }
            }
            return true;
        }
    }
    return false;  // 未知形狀（防禦性：不應到達，enum class 窮舉已覆蓋）
}

bool point_in_clip_region(const ClipRegion& region, const NormPoint& point) {
    switch (region.shape) {
        case ClipShape::Rect:
            return point.x >= region.rect.x0 && point.x <= region.rect.x1 &&
                   point.y >= region.rect.y0 && point.y <= region.rect.y1;
        case ClipShape::RoundedRect: {
            const bool in_bbox = point.x >= region.rect.x0 && point.x <= region.rect.x1 &&
                                  point.y >= region.rect.y0 && point.y <= region.rect.y1;
            if (!in_bbox) {
                return false;
            }
            return point_in_rounded_rect(region.rect, region.corner_radius, point);
        }
        case ClipShape::Path:
            return point_in_polygon(region.path, point);
    }
    return false;
}

// --- ClipMaskService：裁切 ---
ClipStatus ClipMaskService::set_clip(const ContainerId& container, const ClipRegion& region) {
    if (container.empty() || !is_valid_clip_region(region)) {
        return ClipStatus::Invalid;  // 無效裁切區域明確報錯，不靜默接受
    }
    clips_[container] = region;
    return ClipStatus::Ok;
}

ClipStatus ClipMaskService::clear_clip(const ContainerId& container) {
    clips_.erase(container);
    return ClipStatus::Ok;
}

bool ClipMaskService::has_clip(const ContainerId& container) const {
    return clips_.find(container) != clips_.end();
}

const ClipRegion* ClipMaskService::clip_region(const ContainerId& container) const {
    auto it = clips_.find(container);
    return it != clips_.end() ? &it->second : nullptr;
}

// --- ClipMaskService：巢狀裁切 ---
ClipStatus ClipMaskService::set_parent(const ContainerId& container, const ContainerId& parent) {
    if (container.empty() || parent.empty() || container == parent) {
        return ClipStatus::Invalid;  // 空 id 或自我父代
    }
    // 迴圈偵測：沿 parent 既有父代鏈往上走，若走到 container 即會形成迴圈。
    ContainerId cur = parent;
    std::size_t guard = 0;
    const std::size_t max_steps = parents_.size() + 1;
    while (!cur.empty()) {
        if (cur == container) {
            return ClipStatus::Invalid;  // 會形成迴圈父代鏈
        }
        auto it = parents_.find(cur);
        if (it == parents_.end()) {
            break;
        }
        cur = it->second;
        if (++guard > max_steps) {
            break;  // 防禦性：既有鏈本應無環（迴圈已在建立時被擋）
        }
    }
    parents_[container] = parent;
    return ClipStatus::Ok;
}

ClipStatus ClipMaskService::clear_parent(const ContainerId& container) {
    parents_.erase(container);
    return ClipStatus::Ok;
}

ContainerId ClipMaskService::parent_of(const ContainerId& container) const {
    auto it = parents_.find(container);
    return it != parents_.end() ? it->second : ContainerId{};
}

// --- ClipMaskService：遮罩 ---
ClipStatus ClipMaskService::apply_mask(const ContainerId& container, const MaskSource& mask) {
    if (container.empty() || !finite(mask.coverage)) {
        return ClipStatus::Invalid;
    }
    if (mask.kind == MaskKind::NamedPattern && mask.pattern_name.empty()) {
        return ClipStatus::Invalid;  // 具名圖樣必須有名字
    }
    MaskSource normalized = mask;
    normalized.coverage = std::clamp(mask.coverage, 0.0f, 1.0f);
    masks_[container] = normalized;
    return ClipStatus::Ok;
}

ClipStatus ClipMaskService::clear_mask(const ContainerId& container) {
    masks_.erase(container);
    return ClipStatus::Ok;
}

bool ClipMaskService::has_mask(const ContainerId& container) const {
    return masks_.find(container) != masks_.end();
}

const MaskSource* ClipMaskService::mask_source(const ContainerId& container) const {
    auto it = masks_.find(container);
    return it != masks_.end() ? &it->second : nullptr;
}

// --- ClipMaskService：命中 / 可見判定 ---
bool ClipMaskService::is_visible(const ContainerId& container, const NormPoint& point) const {
    if (!in_unit_range(point.x) || !in_unit_range(point.y)) {
        return false;  // 局部正規化空間外一律不可見，不靜默假設
    }
    // 自身 + 沿父代鏈祖先的裁切（共享具名局部座標的簡化假設，見標頭說明）。
    ContainerId cur = container;
    std::size_t guard = 0;
    const std::size_t max_steps = parents_.size() + 1;
    while (!cur.empty()) {
        const ClipRegion* region = clip_region(cur);
        if (region != nullptr && !point_in_clip_region(*region, point)) {
            return false;
        }
        auto it = parents_.find(cur);
        if (it == parents_.end()) {
            break;
        }
        cur = it->second;
        if (++guard > max_steps) {
            break;  // 防禦性：迴圈已在 set_parent 建立時被擋
        }
    }
    const MaskSource* mask = mask_source(container);
    if (mask != nullptr && mask->coverage <= 0.0f) {
        return false;  // 完全被遮罩蓋住
    }
    return true;
}

// --- ClipMaskService：render model ---
std::vector<ClipMaskService::RenderEntry> ClipMaskService::render_model() const {
    std::map<ContainerId, RenderEntry> entries;  // std::map 依鍵字典序 -> 決定性順序

    auto get_or_create = [&entries](const ContainerId& id) -> RenderEntry& {
        auto it = entries.find(id);
        if (it == entries.end()) {
            RenderEntry entry;
            entry.container = id;
            it = entries.emplace(id, std::move(entry)).first;
        }
        return it->second;
    };

    for (const auto& [id, region] : clips_) {
        RenderEntry& entry = get_or_create(id);
        entry.has_clip = true;
        entry.clip = region;
    }
    for (const auto& [id, mask] : masks_) {
        RenderEntry& entry = get_or_create(id);
        entry.has_mask = true;
        entry.mask = mask;
    }
    for (const auto& [id, parent] : parents_) {
        RenderEntry& entry = get_or_create(id);
        entry.parent = parent;
    }

    std::vector<RenderEntry> out;
    out.reserve(entries.size());
    for (auto& [id, entry] : entries) {
        (void)id;
        out.push_back(std::move(entry));
    }
    return out;
}

}  // namespace ds::render
