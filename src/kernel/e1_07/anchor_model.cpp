// E1-07 anchor 定位模型 — 實作（純佈局計算 + 具名 surface 定位服務）
//
// 相位 1：無真實視窗 / 繪圖 API、無平台分支（無 #ifdef / win32 / cocoa）。所有數學為
// 平台中立的正規化佈局計算。無效輸入結構化回報，不靜默、不崩潰。
#include "anchor_model.hpp"

#include <cmath>  // std::isfinite

namespace ds::kernel {

namespace {

// 錨點列舉的界（九宮 = 0..8）。
constexpr int kAnchorMin = static_cast<int>(Anchor::TopLeft);      // 0
constexpr int kAnchorMax = static_cast<int>(Anchor::BottomRight);  // 8

}  // namespace

bool is_valid_anchor(Anchor a) {
    const int v = static_cast<int>(a);
    return v >= kAnchorMin && v <= kAnchorMax;
}

bool anchor_fraction(Anchor a, float& fx, float& fy) {
    if (!is_valid_anchor(a)) {
        return false;  // 不觸碰 out（不靜默）
    }
    // 水平分數：左 = 0、中 = 0.5、右 = 1。
    switch (a) {
        case Anchor::TopLeft:
        case Anchor::CenterLeft:
        case Anchor::BottomLeft:
            fx = 0.0f;
            break;
        case Anchor::TopCenter:
        case Anchor::Center:
        case Anchor::BottomCenter:
            fx = 0.5f;
            break;
        case Anchor::TopRight:
        case Anchor::CenterRight:
        case Anchor::BottomRight:
            fx = 1.0f;
            break;
    }
    // 垂直分數：上 = 0、中 = 0.5、下 = 1。
    switch (a) {
        case Anchor::TopLeft:
        case Anchor::TopCenter:
        case Anchor::TopRight:
            fy = 0.0f;
            break;
        case Anchor::CenterLeft:
        case Anchor::Center:
        case Anchor::CenterRight:
            fy = 0.5f;
            break;
        case Anchor::BottomLeft:
        case Anchor::BottomCenter:
        case Anchor::BottomRight:
            fy = 1.0f;
            break;
    }
    return true;
}

float spacing_fraction(Spacing s) {
    switch (s) {
        case Spacing::None:
            return 0.0f;
        case Spacing::Tight:
            return 0.01f;
        case Spacing::Snug:
            return 0.02f;
        case Spacing::Cozy:
            return 0.04f;
        case Spacing::Roomy:
            return 0.08f;
        case Spacing::Spacious:
            return 0.12f;
    }
    return 0.0f;  // 未知保守回 0
}

Offset Offset::from_spacing(Spacing sx, Spacing sy) {
    return Offset{spacing_fraction(sx), spacing_fraction(sy)};
}

Offset inset_from(Anchor anchor, Spacing amount) {
    float fx = 0.0f;
    float fy = 0.0f;
    if (!anchor_fraction(anchor, fx, fy)) {
        return Offset{};  // 無效 anchor → 零偏移
    }
    const float s = spacing_fraction(amount);
    // 靠邊則向內側縮排；置中軸不動。
    const float dx = (fx == 0.0f) ? s : (fx == 1.0f ? -s : 0.0f);
    const float dy = (fy == 0.0f) ? s : (fy == 1.0f ? -s : 0.0f);
    return Offset{dx, dy};
}

bool is_finite_size(const Size& s) {
    return std::isfinite(s.width) && std::isfinite(s.height) && s.width >= 0.0f &&
           s.height >= 0.0f;
}

namespace {

// spec 本身（不含尺寸）是否合法：anchor 合法且 offset 有限。
bool is_valid_spec(const AnchorSpec& spec) {
    return is_valid_anchor(spec.anchor) && std::isfinite(spec.offset.dx) &&
           std::isfinite(spec.offset.dy);
}

}  // namespace

AnchorStatus resolve(const AnchorSpec& spec, const Size& container, const Size& element,
                     ResolvedPlacement& out) {
    if (!is_valid_spec(spec)) {
        return AnchorStatus::Invalid;
    }
    if (!is_finite_size(container) || !is_finite_size(element)) {
        return AnchorStatus::Invalid;
    }
    float fx = 0.0f;
    float fy = 0.0f;
    anchor_fraction(spec.anchor, fx, fy);  // 已由 is_valid_spec 保證成功

    out.width = element.width;
    out.height = element.height;
    // 具名錨點對齊（元件自身參考點對齊容器參考點）+ 正規化相對偏移。
    out.x = fx * (container.width - element.width) + spec.offset.dx * container.width;
    out.y = fy * (container.height - element.height) + spec.offset.dy * container.height;
    return AnchorStatus::Ok;
}

AnchorStatus resolve_point(const AnchorSpec& spec, const Size& container, float& x, float& y) {
    ResolvedPlacement placement;
    const AnchorStatus st = resolve(spec, container, Size{0.0f, 0.0f}, placement);
    if (st != AnchorStatus::Ok) {
        return st;  // 不觸碰 out（不靜默）
    }
    x = placement.x;
    y = placement.y;
    return AnchorStatus::Ok;
}

// ---------------------------------------------------------------------------
// AnchorLayout
// ---------------------------------------------------------------------------

AnchorLayout::Record* AnchorLayout::find(const SurfaceId& id) {
    for (auto& r : records_) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

const AnchorLayout::Record* AnchorLayout::find(const SurfaceId& id) const {
    for (const auto& r : records_) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

AnchorStatus AnchorLayout::place(const SurfaceId& id, const AnchorSpec& spec) {
    if (id.empty() || !is_valid_spec(spec)) {
        return AnchorStatus::Invalid;
    }
    if (Record* existing = find(id)) {
        existing->spec = spec;  // 就地更新
        return AnchorStatus::Ok;
    }
    records_.push_back(Record{id, spec});
    return AnchorStatus::Ok;
}

AnchorStatus AnchorLayout::remove(const SurfaceId& id) {
    for (auto it = records_.begin(); it != records_.end(); ++it) {
        if (it->id == id) {
            records_.erase(it);
            return AnchorStatus::Ok;
        }
    }
    return AnchorStatus::Invalid;  // 未知 id
}

const AnchorSpec* AnchorLayout::spec_of(const SurfaceId& id) const {
    const Record* r = find(id);
    return r ? &r->spec : nullptr;
}

AnchorStatus AnchorLayout::resolve_for(const SurfaceId& id, const Size& container,
                                       const Size& element, ResolvedPlacement& out) const {
    const Record* r = find(id);
    if (r == nullptr) {
        return AnchorStatus::Invalid;  // 未知 id
    }
    return resolve(r->spec, container, element, out);
}

}  // namespace ds::kernel
