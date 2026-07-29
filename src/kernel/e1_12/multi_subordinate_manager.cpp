// E1-12 多重從屬 surface 並存 — 實作（多子並存的列舉 + 批次操作，委由 E1-11 做附著記錄
// 與相對定位計算）
//
// 相位 1：無真實視窗 / 繪圖 API、無平台分支（無 #ifdef / win32 / cocoa）。純記憶體記錄 +
// 委由 E1-11 SubordinateLayout 做附著管理與 E1-07 resolve() 做佈局計算。無效輸入結構化
// 回報，不靜默、不崩潰。
#include "multi_subordinate_manager.hpp"

namespace ds::kernel {

void MultiSubordinateManager::set_element_size(const SurfaceId& child, const Size& size) {
    for (auto& entry : element_sizes_) {
        if (entry.first == child) {
            entry.second = size;  // 就地更新（同 E1-11 attach 的就地更新語意）
            return;
        }
    }
    element_sizes_.emplace_back(child, size);
}

void MultiSubordinateManager::erase_element_size(const SurfaceId& child) {
    for (auto it = element_sizes_.begin(); it != element_sizes_.end(); ++it) {
        if (it->first == child) {
            element_sizes_.erase(it);
            return;
        }
    }
}

void MultiSubordinateManager::prune_element_sizes() {
    // 移除所有「已不在 layout_ 附著記錄中」的元件尺寸記錄——供 detach_all（委由 E1-11
    // close_parent 遞迴清除一批 child）之後，同步清掉那些連帶被移除之 child 的尺寸記錄，
    // 不留孤兒資料。
    for (auto it = element_sizes_.begin(); it != element_sizes_.end();) {
        if (!layout_.is_attached(it->first)) {
            it = element_sizes_.erase(it);
        } else {
            ++it;
        }
    }
}

AnchorStatus MultiSubordinateManager::attach_child(const SurfaceId& parent, const SurfaceId& child,
                                                   const AnchorSpec& spec,
                                                   const Size& child_element) {
    if (!is_finite_size(child_element)) {
        return AnchorStatus::Invalid;  // 元件尺寸非有限 / 負值：不記錄，不靜默
    }
    const AnchorStatus st = layout_.attach(child, parent, spec);
    if (st != AnchorStatus::Ok) {
        return st;  // 空 id / 自附 / 循環 / spec 無效：E1-11 已拒絕，不變更既有記錄
    }
    set_element_size(child, child_element);
    return AnchorStatus::Ok;
}

std::vector<SurfaceId> MultiSubordinateManager::children_of(const SurfaceId& parent) const {
    std::vector<SurfaceId> result;
    // 依 element_sizes_ 的登錄順序（= 附著順序）列舉，過濾出目前 parent_of 為 parent 者。
    // element_sizes_ 只透過 attach_child 新增，故涵蓋所有經本管理層附著的 child。
    for (const auto& entry : element_sizes_) {
        const SurfaceId* p = layout_.parent_of(entry.first);
        if (p != nullptr && *p == parent) {
            result.push_back(entry.first);
        }
    }
    return result;
}

std::vector<std::pair<SurfaceId, ResolvedPlacement>> MultiSubordinateManager::reposition_all(
    const SurfaceId& parent, const ResolvedPlacement& parent_placement) const {
    std::vector<std::pair<SurfaceId, ResolvedPlacement>> result;
    for (const SurfaceId& child : children_of(parent)) {
        const Size* element = element_size_of(child);
        if (element == nullptr) {
            continue;  // 理論上不會發生（attach_child 必先記錄尺寸才會出現於 children_of）
        }
        ResolvedPlacement out;
        if (layout_.resolve_child(child, parent_placement, *element, out) == AnchorStatus::Ok) {
            result.emplace_back(child, out);
        }
        // 解析失敗（理論上不會，因為 attach_child 已驗證過 spec 與元件尺寸）→ 略過該子，
        // 不中斷其餘子的批次重新解析。
    }
    return result;
}

AnchorStatus MultiSubordinateManager::detach_child(const SurfaceId& child) {
    const AnchorStatus st = layout_.detach(child);
    if (st == AnchorStatus::Ok) {
        erase_element_size(child);
    }
    return st;  // 未知 child → Invalid（不崩潰），本管理層的元件尺寸記錄本就不會有該筆
}

std::size_t MultiSubordinateManager::detach_all(const SurfaceId& parent) {
    const std::size_t removed = layout_.close_parent(parent);
    prune_element_sizes();  // 同步清掉所有隨之連帶移除之 child 的元件尺寸記錄
    return removed;
}

const Size* MultiSubordinateManager::element_size_of(const SurfaceId& child) const {
    for (const auto& entry : element_sizes_) {
        if (entry.first == child) {
            return &entry.second;
        }
    }
    return nullptr;
}

}  // namespace ds::kernel
