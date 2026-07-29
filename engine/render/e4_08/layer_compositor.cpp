// E4-08 圖層合成 — 實作（見 layer_compositor.hpp 規格）。
#include "layer_compositor.hpp"

#include <cmath>     // std::isfinite
#include <utility>   // std::move

namespace ds::render {

namespace {

// 混合模式合法性 —— 僅 Normal/Multiply/Screen/Overlay 四個具名值合法（NFR-02：具名列舉，
// 非數字係數）；防禦性檢查以擋下呼叫端以 `static_cast` 硬塞的非法列舉值，不靜默接受。
bool valid_blend_mode(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal:
        case BlendMode::Multiply:
        case BlendMode::Screen:
        case BlendMode::Overlay:
            return true;
    }
    return false;
}

// 把透明度 clamp 至 [0,1]（呼叫前已保證為有限值）。
float clamp_opacity(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

}  // namespace

// --- 內部尋找（具名鍵線性掃描；同子系統 / 同建置慣例的圖層數量小，見 E1-03 / E4-06）---
std::vector<LayerEntry>::iterator LayerCompositor::find(const ds::kernel::SurfaceId& id) {
    for (auto it = layers_.begin(); it != layers_.end(); ++it) {
        if (it->surface_id == id) {
            return it;
        }
    }
    return layers_.end();
}

std::vector<LayerEntry>::const_iterator LayerCompositor::find(
    const ds::kernel::SurfaceId& id) const {
    for (auto it = layers_.begin(); it != layers_.end(); ++it) {
        if (it->surface_id == id) {
            return it;
        }
    }
    return layers_.end();
}

// --- 加入 / 移除圖層 ---
CompositeStatus LayerCompositor::add_layer(const ds::kernel::SurfaceId& surface_id,
                                            BlendMode blend_mode, float opacity) {
    if (surface_id.empty()) {
        return CompositeStatus::Invalid;  // 空 id：不加入
    }
    if (!valid_blend_mode(blend_mode)) {
        return CompositeStatus::Invalid;  // 未知混合模式：不加入
    }
    if (!std::isfinite(opacity)) {
        return CompositeStatus::Invalid;  // NaN / Inf：不靜默改成預設值，不加入
    }
    if (find(surface_id) != layers_.end()) {
        return CompositeStatus::Invalid;  // 同一 surface 已是一層：不覆蓋、不新增第二筆
    }
    if (!switcher_.has(surface_id)) {
        return CompositeStatus::NotFound;  // 未經 E4-06 定址存在：不加入
    }

    LayerEntry entry;
    entry.surface_id = surface_id;
    entry.blend_mode = blend_mode;
    entry.opacity = clamp_opacity(opacity);
    layers_.push_back(std::move(entry));
    return CompositeStatus::Ok;
}

CompositeStatus LayerCompositor::remove_layer(const ds::kernel::SurfaceId& surface_id) {
    auto it = find(surface_id);
    if (it == layers_.end()) {
        return CompositeStatus::NotFound;  // 本合成器內未知圖層：不崩潰
    }
    layers_.erase(it);
    return CompositeStatus::Ok;
}

// --- 查詢 ---
bool LayerCompositor::has_layer(const ds::kernel::SurfaceId& surface_id) const {
    return find(surface_id) != layers_.end();
}

std::vector<ds::kernel::SurfaceId> LayerCompositor::layer_order() const {
    std::vector<ds::kernel::SurfaceId> order;
    order.reserve(layers_.size());
    for (const auto& entry : layers_) {
        order.push_back(entry.surface_id);
    }
    return order;  // 依加入順序的具名清單，不外露任何內部數字索引語意
}

// --- 合成計畫 ---
CompositionPlan LayerCompositor::compose() const {
    CompositionPlan plan;
    plan.layers = layers_;  // 複本：依加入順序排列的圖層描述，純描述、不做真實像素合成
    return plan;
}

}  // namespace ds::render
