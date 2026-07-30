// E1-11 從屬 surface 相對定位 — 實作（父子附著記錄 + 相對佈局解析）
//
// 相位 1：無真實視窗 / 繪圖 API、無平台分支（無 #ifdef / win32 / cocoa）。純記憶體記錄配對 +
// 委由 E1-07 resolve() 做純佈局計算。無效輸入結構化回報，不靜默、不崩潰。
#include "subordinate_layout.hpp"

namespace ds::kernel {

SubordinateLayout::Record* SubordinateLayout::find(const SurfaceId& child) {
    for (auto& r : records_) {
        if (r.child == child) {
            return &r;
        }
    }
    return nullptr;
}

const SubordinateLayout::Record* SubordinateLayout::find(const SurfaceId& child) const {
    for (const auto& r : records_) {
        if (r.child == child) {
            return &r;
        }
    }
    return nullptr;
}

bool SubordinateLayout::would_cycle(const SurfaceId& child, const SurfaceId& parent) const {
    // 從 parent 沿既有附著鏈往上追溯；若曾經抵達 child，即為循環（含 parent == child 的自附，
    // 因為第一步就會命中）。步數界為 records_.size() + 1：合法鏈長不可能超過既有記錄數，
    // 超界視為異常資料，保守回 true（不放行，不無限迴圈）。
    SurfaceId cursor = parent;
    const std::size_t guard = records_.size() + 1;
    for (std::size_t i = 0; i < guard; ++i) {
        if (cursor == child) {
            return true;
        }
        const SurfaceId* next = parent_of(cursor);
        if (next == nullptr) {
            return false;  // 追到頂（非任何附著的 child）：沒有繞回 child，不是循環
        }
        cursor = *next;
    }
    return true;  // 防禦：理論上不應到達（每次 attach 皆先過此檢查）
}

namespace {

// spec 本身（不含尺寸）是否合法：anchor 合法且 offset 有限。與 E1-07 內部 is_valid_spec 判定
// 等價（該判定為 e1_07 匿名命名空間私有符號，故此處以公開的 resolve() 等價重建，
// 不觸碰上游實作）。
bool is_valid_spec(const AnchorSpec& spec) {
    ResolvedPlacement probe;
    // 以中性的正尺寸容器 / 零尺寸元件探測 resolve() 的合法性判定（anchor 越界 / offset 非有限
    // 皆會使 resolve() 回 Invalid）；比重寫一份判定邏輯更不容易與上游脫節。
    return resolve(spec, Size{1.0f, 1.0f}, Size{0.0f, 0.0f}, probe) == AnchorStatus::Ok;
}

}  // namespace

AnchorStatus SubordinateLayout::attach(const SurfaceId& child, const SurfaceId& parent,
                                       const AnchorSpec& spec) {
    if (child.empty() || parent.empty() || !is_valid_spec(spec)) {
        return AnchorStatus::Invalid;
    }
    if (would_cycle(child, parent)) {
        return AnchorStatus::Invalid;  // 自附或循環附著：不記錄（不靜默）
    }
    if (Record* existing = find(child)) {
        existing->parent = parent;  // 就地更新（可換 parent、可換 spec）
        existing->spec = spec;
        return AnchorStatus::Ok;
    }
    records_.push_back(Record{child, parent, spec});
    return AnchorStatus::Ok;
}

AnchorStatus SubordinateLayout::detach(const SurfaceId& child) {
    for (auto it = records_.begin(); it != records_.end(); ++it) {
        if (it->child == child) {
            records_.erase(it);
            return AnchorStatus::Ok;
        }
    }
    return AnchorStatus::Invalid;  // 未知 child
}

AnchorStatus SubordinateLayout::reposition(const SurfaceId& child, const AnchorSpec& spec) {
    if (!is_valid_spec(spec)) {
        return AnchorStatus::Invalid;
    }
    Record* existing = find(child);
    if (existing == nullptr) {
        return AnchorStatus::Invalid;  // 未知 child
    }
    existing->spec = spec;  // parent 不變
    return AnchorStatus::Ok;
}

std::size_t SubordinateLayout::close_parent(const SurfaceId& id) {
    std::size_t removed = 0;

    // id 自身若也是別人的子（巢狀從屬鏈中間節點），連帶清除其自身的附著記錄。
    if (detach(id) == AnchorStatus::Ok) {
        ++removed;
    }

    // 廣度優先遞迴清除：反覆收集「parent 落在目前 frontier 中」的記錄，逐層往下清。
    std::vector<SurfaceId> frontier{id};
    while (!frontier.empty()) {
        std::vector<SurfaceId> next_frontier;
        std::vector<SurfaceId> to_remove;
        for (const auto& r : records_) {
            for (const auto& f : frontier) {
                if (r.parent == f) {
                    to_remove.push_back(r.child);
                    break;
                }
            }
        }
        for (const auto& child_id : to_remove) {
            if (detach(child_id) == AnchorStatus::Ok) {
                ++removed;
                next_frontier.push_back(child_id);
            }
        }
        frontier = std::move(next_frontier);
    }

    return removed;
}

AnchorStatus SubordinateLayout::resolve_child(const SurfaceId& child,
                                              const ResolvedPlacement& parent_placement,
                                              const Size& child_element,
                                              ResolvedPlacement& out) const {
    const Record* r = find(child);
    if (r == nullptr) {
        return AnchorStatus::Invalid;  // 未知 child（不寫 out）
    }
    const Size parent_size{parent_placement.width, parent_placement.height};
    ResolvedPlacement local;
    const AnchorStatus st = resolve(r->spec, parent_size, child_element, local);
    if (st != AnchorStatus::Ok) {
        return st;  // 非有限 parent 尺寸 / 元件尺寸等 → Invalid（不寫 out）
    }
    // 平移：父的局部座標系 → 呼叫端座標系（父矩形的絕對原點）。
    out.x = parent_placement.x + local.x;
    out.y = parent_placement.y + local.y;
    out.width = local.width;
    out.height = local.height;
    return AnchorStatus::Ok;
}

const SurfaceId* SubordinateLayout::parent_of(const SurfaceId& child) const {
    const Record* r = find(child);
    return r ? &r->parent : nullptr;
}

const AnchorSpec* SubordinateLayout::spec_of(const SurfaceId& child) const {
    const Record* r = find(child);
    return r ? &r->spec : nullptr;
}

bool SubordinateLayout::has_children(const SurfaceId& id) const {
    for (const auto& r : records_) {
        if (r.parent == id) {
            return true;
        }
    }
    return false;
}

}  // namespace ds::kernel
