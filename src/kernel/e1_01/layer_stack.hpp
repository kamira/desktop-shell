// E1-01 具名圖層列舉與 z-order 維持 — 宣告檔（platform 層 / 子系統 kernel / 相位 1 null）
//
// 桌面元件分屬**具名圖層**（named layers：桌布 / 一般視窗之下 / 一般 / 浮層 / 最上層），
// 堆疊順序（stacking order）由**圖層語意**決定，而非由呼叫端指定的數字層級。本單元建於
// 上游 E1-24 的具名圖層詞彙（`SurfaceLayer` / `SurfaceId`）之上，提供：
//   1. 列舉具名圖層（由底到頂的語意序，附穩定具名字串）。
//   2. 把具名 surface 指派到具名圖層。
//   3. 查詢 / 維持整體堆疊順序（由底到頂）。
//   4. 圖層間相對順序規則（Below / Same / Above），與同層內、跨層的 surface 相對順序。
//
// 核心不變式：**圖層語意優先於加入序**——跨層先依圖層語意排序，同層內才依加入序（穩定）。
//
// 硬約束（NFR-02）：對外 API **只有具名圖層**，不出現數字 z-order / z-index，不出現絕對
//   座標；圖層相對順序以**具名關係** `LayerRelation` 表達，surface 一律以具名 `SurfaceId`
//   指涉。內部排序不以呼叫端可見的數字層級進行，而是掃描「由底到頂具名圖層清單」得出。
// 硬約束（NFR-03）：改動堆疊狀態的操作一律先經 `has()` 能力閘控（`kernel.surface`）；
//   能力不可用時操作被結構化拒絕、不改任何狀態、絕不崩潰。
//
// 相位 1（Mac / null 期）：純介面 + 記憶體維護，無 `#ifdef` / win32 / cocoa 平台分支，
//   不觸碰任何真實 OS 視窗 / 合成器 API。
#ifndef DS_KERNEL_E1_01_LAYER_STACK_HPP
#define DS_KERNEL_E1_01_LAYER_STACK_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "capability_matrix.hpp"  // 上游 E1-21：CapabilityMatrix / CapabilityId / has()（可讀不可改）
#include "null_backend.hpp"       // 上游 E1-24：SurfaceLayer（具名圖層）/ SurfaceId（可讀不可改）

namespace ds::kernel {

// 本單元閘控用的能力 id：surface / 圖層核心（E1-21 宣告為保證存在的基礎能力）。
// NFR-03：改動堆疊狀態的操作先 has(kLayerCapability) 才執行。
inline const CapabilityId& layer_capability() {
    static const CapabilityId kId = "kernel.surface";
    return kId;
}

// 兩個具名圖層之間的相對順序（NFR-02：以具名關係表達，不以數字層級表達）。
enum class LayerRelation {
    Below,  // 左者在右者之下（更接近桌布）
    Same,   // 同一具名圖層
    Above,  // 左者在右者之上（更接近最上層）
};

// 指派結果 —— 讓「成功 / 改派 / 空 id 拒絕 / 能力閘控拒絕」在呼叫端可分辨（NFR-03 可觀測）。
enum class LayerAssign {
    Ok,                     // 新 surface 指派成功
    Moved,                  // 既有 surface 改派到（可能不同的）具名圖層
    RejectedEmptyId,        // 空 SurfaceId：保守拒絕
    RejectedNoCapability,   // has(kernel.surface) == false：NFR-03 閘控拒絕，狀態不變
};

// 由底到頂的**具名圖層**語意序（單一資料來源）。索引僅為內部語意序，不對外當數字層級用。
const std::vector<SurfaceLayer>& layers_bottom_to_top();

// 具名圖層的穩定字串名（如 SurfaceLayer::Wallpaper -> "layer.wallpaper"）。
// 供跨模組以「具名」方式指涉圖層（NFR-02），不以數字層級指涉。
std::string layer_name(SurfaceLayer layer);

// 兩具名圖層的相對順序（依 layers_bottom_to_top 的語意序）。
LayerRelation compare_layers(SurfaceLayer lhs, SurfaceLayer rhs);

// ---------------------------------------------------------------------------
// LayerStack —— 具名圖層的堆疊順序維護器（相位 1 記憶體實作）。
//
// 維護「具名 surface -> 具名圖層」的指派，並據**圖層語意優先於加入序**的規則維持一條
// 由底到頂的堆疊順序。所有相對順序查詢皆以具名圖層 / 具名 surface 表達，對外不暴露任何
// 數字 z-order。改動狀態的操作經 `has(kernel.surface)` 閘控（NFR-03）。
// ---------------------------------------------------------------------------
class LayerStack {
public:
    // 預設以 E1-21 內嵌能力矩陣建構（保守）；亦可注入自訂矩陣（供測試 NFR-03 兩條路徑）。
    explicit LayerStack(CapabilityMatrix caps = CapabilityMatrix::defaults());

    // --- 能力查詢（NFR-03，經 E1-21）---
    const CapabilityMatrix& capabilities() const noexcept { return caps_; }
    bool has(const CapabilityId& id) const { return caps_.has(id); }

    // --- 指派 / 移除（改動狀態 → 先 has() 閘控）---
    // 把具名 surface 指派到具名圖層。空 id 拒絕；has(kernel.surface)==false 時拒絕且不改狀態。
    // 既有 surface 再指派 = 改派其圖層（保留其在整體加入序中的位置），回 Moved。
    LayerAssign assign(const SurfaceId& id, SurfaceLayer layer);
    // 移除具名 surface 的指派；回傳是否確有移除（未知 id 回 false，不崩潰）。
    // 改動狀態：has(kernel.surface)==false 時一律回 false（不改狀態）。
    bool remove(const SurfaceId& id);

    // --- 查詢（唯讀，不需閘控）---
    // 該具名 surface 是否已指派。
    bool contains(const SurfaceId& id) const;
    // 該具名 surface 所屬的具名圖層；未指派回 nullptr。
    const SurfaceLayer* layer_of(const SurfaceId& id) const;
    // 目前指派的 surface 總數。
    std::size_t size() const noexcept { return entries_.size(); }
    // 某具名圖層內的 surface 數量。
    std::size_t count_in(SurfaceLayer layer) const;

    // --- 堆疊順序（維持 & 查詢）---
    // 整體堆疊順序，由底到頂。跨層依圖層語意、同層依加入序（穩定）。
    std::vector<SurfaceId> stacking_order() const;
    // 某具名圖層內的 surface（加入序，穩定）。
    std::vector<SurfaceId> ids_in(SurfaceLayer layer) const;
    // 堆疊最頂 / 最底的具名 surface；空堆疊回空字串。
    SurfaceId topmost() const;
    SurfaceId bottommost() const;

    // --- 相對順序規則（具名，不含數字 z-order）---
    // a 是否堆疊在 b 之上 / 之下。任一未指派回 false（保守）。
    bool is_above(const SurfaceId& a, const SurfaceId& b) const;
    bool is_below(const SurfaceId& a, const SurfaceId& b) const;

private:
    // 一筆指派：具名 surface -> 具名圖層。entries_ 的順序即整體加入序（穩定）。
    struct Entry {
        SurfaceId id;
        SurfaceLayer layer;
    };

    Entry* find(const SurfaceId& id);
    const Entry* find(const SurfaceId& id) const;
    // 具名 surface 在整體堆疊順序中的位置（由底到頂，0 起）；未指派回 npos。內部用，不對外。
    std::size_t stack_position(const SurfaceId& id) const;

    CapabilityMatrix caps_;
    // 加入序保存（穩定）；堆疊順序由 stacking_order() 依圖層語意重新導出，不在此排序。
    std::vector<Entry> entries_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_01_LAYER_STACK_HPP
