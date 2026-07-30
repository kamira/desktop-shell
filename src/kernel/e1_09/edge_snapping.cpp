// E1-09 邊緣吸附 — 實作（純幾何吸附邏輯 + 宣告式再表達 + E1-08 整合便利層）
//
// 相位 1：無真實視窗 / 繪圖 / OS API、無平台分支（無 #ifdef / win32 / cocoa）。所有數學為
// 平台中立的正規化 / 幾何計算；具體像素只在 `resolve` 邊界（呼叫端提供尺寸）出現。無效輸入
// 結構化回報，不靜默、不崩潰。
#include "edge_snapping.hpp"

#include <cmath>  // std::isfinite, std::fabs

namespace ds::kernel {

SnapConfig SnapConfig::from_spacing(Spacing threshold, bool to_screen, bool to_surfaces) {
    SnapConfig cfg;
    cfg.threshold = spacing_fraction(threshold);  // 具名等級 → 正規化分數（NFR-02）
    cfg.to_screen = to_screen;
    cfg.to_surfaces = to_surfaces;
    return cfg;
}

bool screen_edge_of(AxisSnap axis_result, bool horizontal, Edge& out) {
    switch (axis_result) {
        case AxisSnap::ScreenStart:
            out = horizontal ? Edge::Left : Edge::Top;
            return true;
        case AxisSnap::ScreenEnd:
            out = horizontal ? Edge::Right : Edge::Bottom;
            return true;
        case AxisSnap::None:
        case AxisSnap::Surface:
            return false;  // 不觸碰 out（不靜默）
    }
    return false;
}

namespace {

// 矩形（ResolvedPlacement）是否有限且非負尺寸。
bool is_finite_rect(const ResolvedPlacement& r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) &&
           std::isfinite(r.height) && r.width >= 0.0f && r.height >= 0.0f;
}

// spec 本身（不含尺寸）是否合法：anchor 合法且 offset 有限（E1-07 的 is_valid_spec 為內部符號，
// 此處以其公開元件重建等價判定）。
bool is_valid_spec(const AnchorSpec& spec) {
    return is_valid_anchor(spec.anchor) && std::isfinite(spec.offset.dx) &&
           std::isfinite(spec.offset.dy);
}

// 單軸最近邊吸附：dragged 邊 [lo, hi]（lo = 左/上、hi = 右/下）對「螢幕 [0, extent]」與各 target
// 區間 [tlo, thi] 求「夠近（|delta| ≤ thr）且 |delta| 最小」的對齊位移。回傳位移 delta；outcome 記錄
// 吸到哪一類邊（None / ScreenStart / ScreenEnd / Surface）。同距時先到者（螢幕優先）勝。
float best_snap_axis(float lo, float hi, float extent,
                     const std::vector<std::pair<float, float>>& targets, float thr,
                     bool snap_screen, bool snap_surfaces, AxisSnap& outcome) {
    outcome = AxisSnap::None;
    float best_delta = 0.0f;
    float best_abs = thr;  // 只接受 |delta| ≤ thr；以 thr 為初始上界（含等於）
    bool found = false;

    auto consider = [&](float delta, AxisSnap kind) {
        const float a = std::fabs(delta);
        if (a <= best_abs && (!found || a < best_abs)) {
            best_abs = a;
            best_delta = delta;
            outcome = kind;
            found = true;
        }
    };

    if (snap_screen) {
        consider(0.0f - lo, AxisSnap::ScreenStart);   // dragged 起始邊 → 螢幕起始邊 (0)
        consider(extent - hi, AxisSnap::ScreenEnd);   // dragged 末端邊 → 螢幕末端邊 (extent)
    }
    if (snap_surfaces) {
        for (const auto& t : targets) {
            const float tlo = t.first;
            const float thi = t.second;
            consider(tlo - lo, AxisSnap::Surface);  // 起始邊對齊（左-左 / 上-上）
            consider(thi - hi, AxisSnap::Surface);  // 末端邊對齊（右-右 / 下-下）
            consider(thi - lo, AxisSnap::Surface);  // dragged 起始邊貼 target 末端邊（相鄰）
            consider(tlo - hi, AxisSnap::Surface);  // dragged 末端邊貼 target 起始邊（相鄰）
        }
    }
    return found ? best_delta : 0.0f;
}

}  // namespace

SnapStatus snap_rect(const ResolvedPlacement& dragged,
                     const std::vector<ResolvedPlacement>& targets, const Size& container,
                     const SnapConfig& config, SnapResult& out) {
    if (!is_finite_size(container)) {
        return SnapStatus::Invalid;
    }
    if (!std::isfinite(config.threshold) || config.threshold < 0.0f) {
        return SnapStatus::Invalid;
    }
    if (!is_finite_rect(dragged)) {
        return SnapStatus::Invalid;
    }
    std::vector<std::pair<float, float>> tx;
    std::vector<std::pair<float, float>> ty;
    tx.reserve(targets.size());
    ty.reserve(targets.size());
    for (const auto& t : targets) {
        if (!is_finite_rect(t)) {
            return SnapStatus::Invalid;  // 無效目標不靜默略過
        }
        tx.emplace_back(t.x, t.x + t.width);
        ty.emplace_back(t.y, t.y + t.height);
    }

    const float thr_x = config.threshold * container.width;   // 逐軸換算為判定距離（相對 → 具體）
    const float thr_y = config.threshold * container.height;

    AxisSnap ox = AxisSnap::None;
    AxisSnap oy = AxisSnap::None;
    const float dx = best_snap_axis(dragged.x, dragged.x + dragged.width, container.width, tx, thr_x,
                                    config.to_screen, config.to_surfaces, ox);
    const float dy = best_snap_axis(dragged.y, dragged.y + dragged.height, container.height, ty,
                                    thr_y, config.to_screen, config.to_surfaces, oy);

    out.rect = dragged;
    out.rect.x = dragged.x + dx;
    out.rect.y = dragged.y + dy;
    out.x = ox;
    out.y = oy;
    return SnapStatus::Ok;
}

namespace {

// 九宮分數 (fx, fy)（各 ∈ {0, 0.5, 1}）→ 具名錨點。
Anchor anchor_from_fractions(float fx, float fy) {
    const int col = (fx <= 0.25f) ? 0 : (fx >= 0.75f ? 2 : 1);  // 0 左 / 1 中 / 2 右
    const int row = (fy <= 0.25f) ? 0 : (fy >= 0.75f ? 2 : 1);  // 0 上 / 1 中 / 2 下
    static const Anchor grid[3][3] = {
        {Anchor::TopLeft, Anchor::TopCenter, Anchor::TopRight},
        {Anchor::CenterLeft, Anchor::Center, Anchor::CenterRight},
        {Anchor::BottomLeft, Anchor::BottomCenter, Anchor::BottomRight},
    };
    return grid[row][col];
}

}  // namespace

SnapStatus snap(const AnchorSpec& dragged, const Size& element,
                const std::vector<SnapTarget>& targets, const Size& container,
                const SnapConfig& config, AnchorSpec& out) {
    // 容器須為正且有限（用以把具體矩形換算回正規化偏移，除數不得為零）。
    if (!is_finite_size(container) || container.width <= 0.0f || container.height <= 0.0f) {
        return SnapStatus::Invalid;
    }
    if (!is_valid_spec(dragged) || !is_finite_size(element)) {
        return SnapStatus::Invalid;
    }

    // 落地被吸附者與各目標為具體矩形（resolve 邊界；resolve 亦驗 spec / 尺寸）。
    ResolvedPlacement drect;
    if (resolve(dragged, container, element, drect) != AnchorStatus::Ok) {
        return SnapStatus::Invalid;
    }
    std::vector<ResolvedPlacement> trects;
    trects.reserve(targets.size());
    for (const auto& t : targets) {
        ResolvedPlacement tr;
        if (resolve(t.spec, container, t.element, tr) != AnchorStatus::Ok) {
            return SnapStatus::Invalid;  // 無效目標不靜默
        }
        trects.push_back(tr);
    }

    SnapResult res;
    const SnapStatus st = snap_rect(drect, trects, container, config, res);
    if (st != SnapStatus::Ok) {
        return st;
    }

    // 再表達為 AnchorSpec：吸螢幕末端邊者用末端錨（分數 1）使其「隨容器縮放恆貼齊」；其餘軸用
    // 起始錨（分數 0）+ 正規化偏移精確承載幾何位置（round-trip 回同一矩形）。
    const float fx = (res.x == AxisSnap::ScreenEnd) ? 1.0f : 0.0f;
    const float fy = (res.y == AxisSnap::ScreenEnd) ? 1.0f : 0.0f;
    const float dx =
        (res.rect.x - fx * (container.width - element.width)) / container.width;
    const float dy =
        (res.rect.y - fy * (container.height - element.height)) / container.height;

    out.anchor = anchor_from_fractions(fx, fy);
    out.offset = Offset{dx, dy};
    return SnapStatus::Ok;
}

SnapStatus snap_surface(const EdgeSnapping& snapping, const DraggableSurface& surfaces,
                        const SurfaceId& dragged, const Size& dragged_element,
                        const std::vector<SurfaceTarget>& targets, const Size& container,
                        AnchorSpec& out) {
    const AnchorSpec* live = surfaces.live_position(dragged);
    if (live == nullptr) {
        return SnapStatus::Invalid;  // 未註冊且未拖曳：無實時位置可吸附
    }
    std::vector<SnapTarget> snap_targets;
    snap_targets.reserve(targets.size());
    for (const auto& t : targets) {
        if (t.id == dragged) {
            continue;  // 不對自己吸附
        }
        const AnchorSpec* tspec = surfaces.live_position(t.id);
        if (tspec == nullptr) {
            continue;  // 該目標無實時位置：略過（非錯誤，不列為候選）
        }
        snap_targets.push_back(SnapTarget{*tspec, t.element});
    }
    return snapping.snap(*live, dragged_element, snap_targets, container, out);
}

}  // namespace ds::kernel
