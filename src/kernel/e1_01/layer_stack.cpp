// E1-01 具名圖層列舉與 z-order 維持 — 實作
//
// 純記憶體維護，無平台分支、無真實 OS API。堆疊順序不以數字層級儲存，而是每次由
// 「由底到頂具名圖層清單」＋加入序重新導出，確保**圖層語意優先於加入序**（NFR-02）。
#include "layer_stack.hpp"

#include <limits>
#include <utility>

namespace ds::kernel {

const std::vector<SurfaceLayer>& layers_bottom_to_top() {
    // 具名圖層的語意序（由底到頂）：桌布 → 一般視窗之下 → 一般 → 浮層 → 最上層。
    // 這是本單元「列舉具名圖層」與所有相對順序判斷的單一資料來源。
    static const std::vector<SurfaceLayer> kLayers = {
        SurfaceLayer::Wallpaper,
        SurfaceLayer::BelowNormal,
        SurfaceLayer::Normal,
        SurfaceLayer::Overlay,
        SurfaceLayer::Topmost,
    };
    return kLayers;
}

std::string layer_name(SurfaceLayer layer) {
    switch (layer) {
        case SurfaceLayer::Wallpaper:   return "layer.wallpaper";
        case SurfaceLayer::BelowNormal: return "layer.below_normal";
        case SurfaceLayer::Normal:      return "layer.normal";
        case SurfaceLayer::Overlay:     return "layer.overlay";
        case SurfaceLayer::Topmost:     return "layer.topmost";
    }
    return "layer.unknown";  // 不可達（列舉已窮盡）；保守回退。
}

namespace {

// 具名圖層在語意序中的位置（由底到頂）。純內部：不對外暴露，僅供比較與排序用，
// 對外一律以具名 LayerRelation / 具名 SurfaceId 表達（NFR-02）。
std::size_t semantic_index(SurfaceLayer layer) {
    const std::vector<SurfaceLayer>& order = layers_bottom_to_top();
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == layer) {
            return i;
        }
    }
    return order.size();  // 不可達。
}

}  // namespace

LayerRelation compare_layers(SurfaceLayer lhs, SurfaceLayer rhs) {
    const std::size_t a = semantic_index(lhs);
    const std::size_t b = semantic_index(rhs);
    if (a < b) return LayerRelation::Below;
    if (a > b) return LayerRelation::Above;
    return LayerRelation::Same;
}

LayerStack::LayerStack(CapabilityMatrix caps) : caps_(std::move(caps)) {}

LayerStack::Entry* LayerStack::find(const SurfaceId& id) {
    for (Entry& e : entries_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

const LayerStack::Entry* LayerStack::find(const SurfaceId& id) const {
    for (const Entry& e : entries_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

LayerAssign LayerStack::assign(const SurfaceId& id, SurfaceLayer layer) {
    if (id.empty()) {
        return LayerAssign::RejectedEmptyId;  // 保守：空 id 一律拒絕。
    }
    // NFR-03：改動堆疊狀態前先經 has() 閘控；能力不可用則拒絕且不改任何狀態。
    if (!has(layer_capability())) {
        return LayerAssign::RejectedNoCapability;
    }
    Entry* existing = find(id);
    if (existing != nullptr) {
        // 改派：更新圖層，保留其在整體加入序中的原位置（同層順序仍以加入序穩定）。
        existing->layer = layer;
        return LayerAssign::Moved;
    }
    entries_.push_back(Entry{id, layer});
    return LayerAssign::Ok;
}

bool LayerStack::remove(const SurfaceId& id) {
    // NFR-03：移除亦為改動狀態的操作，先經 has() 閘控。
    if (!has(layer_capability())) {
        return false;
    }
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->id == id) {
            entries_.erase(it);
            return true;
        }
    }
    return false;  // 未知 id：不崩潰。
}

bool LayerStack::contains(const SurfaceId& id) const {
    return find(id) != nullptr;
}

const SurfaceLayer* LayerStack::layer_of(const SurfaceId& id) const {
    const Entry* e = find(id);
    return e != nullptr ? &e->layer : nullptr;
}

std::size_t LayerStack::count_in(SurfaceLayer layer) const {
    std::size_t n = 0;
    for (const Entry& e : entries_) {
        if (e.layer == layer) {
            ++n;
        }
    }
    return n;
}

std::vector<SurfaceId> LayerStack::stacking_order() const {
    // 由底到頂重新導出：外層走具名圖層語意序，內層走加入序（穩定）。
    // 圖層語意因此**優先於**加入序——不同層的 surface 永遠依圖層排列，與其加入先後無關。
    std::vector<SurfaceId> out;
    out.reserve(entries_.size());
    for (SurfaceLayer layer : layers_bottom_to_top()) {
        for (const Entry& e : entries_) {
            if (e.layer == layer) {
                out.push_back(e.id);
            }
        }
    }
    return out;
}

std::vector<SurfaceId> LayerStack::ids_in(SurfaceLayer layer) const {
    std::vector<SurfaceId> out;
    for (const Entry& e : entries_) {
        if (e.layer == layer) {
            out.push_back(e.id);  // 加入序（穩定）。
        }
    }
    return out;
}

SurfaceId LayerStack::topmost() const {
    const std::vector<SurfaceId> order = stacking_order();
    return order.empty() ? SurfaceId{} : order.back();
}

SurfaceId LayerStack::bottommost() const {
    const std::vector<SurfaceId> order = stacking_order();
    return order.empty() ? SurfaceId{} : order.front();
}

std::size_t LayerStack::stack_position(const SurfaceId& id) const {
    const std::vector<SurfaceId> order = stacking_order();
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == id) {
            return i;
        }
    }
    return std::numeric_limits<std::size_t>::max();  // 未指派。
}

bool LayerStack::is_above(const SurfaceId& a, const SurfaceId& b) const {
    if (!contains(a) || !contains(b)) {
        return false;  // 任一未指派：保守。
    }
    return stack_position(a) > stack_position(b);
}

bool LayerStack::is_below(const SurfaceId& a, const SurfaceId& b) const {
    if (!contains(a) || !contains(b)) {
        return false;  // 任一未指派：保守。
    }
    return stack_position(a) < stack_position(b);
}

}  // namespace ds::kernel
