// E1-11 從屬 surface 相對定位 — 子 surface 相對父 surface 的定位模型
// （platform 相位 1 = Mac / null 期）
//
// 語意：**從屬（subordinate）surface** —— tooltip / 下拉選單 / 子面板等**依附於某個父 surface**
// 而存在的 surface。子以**具名 anchor（E1-07）相對父定位**，而非相對螢幕的絕對座標；
// 父移動時子跟隨（因為子的位置是「相對父」重新解析而得，不是快取的絕對座標）、父關閉時子
// 連帶清除（不留孤兒 surface）。
//
// 建於上游 E1-07 `AnchorSpec` / `resolve()` 之上：本單元**不重新發明**錨點定位數學，而是把
// E1-07 的「元件相對容器」語意重新詮釋為「子 surface 相對父 surface」——父的已解析矩形
// （`ResolvedPlacement`）即扮演 E1-07 `resolve()` 的 `container`，子的 anchor + 偏移在此局部
// 座標系中解析後，再平移（+parent.x, +parent.y）成為呼叫端座標系下的絕對矩形。因此：
//   - **NFR-02**：宣告式附著記錄（`AnchorSpec`）本身無絕對座標，具體像素僅在 `resolve_child()`
//     這個計算邊界、由呼叫端提供的「父已解析矩形」與「子元件尺寸」推導而得。
//   - **父移動子跟隨**：本服務不快取子的絕對座標，每次呼叫 `resolve_child()` 都以**當下**的
//     父矩形重新計算——父矩形變了，下一次解析自然跟著變，無需額外同步機制。
//   - **父關閉子連帶**：`close_parent()` 遞迴清除該 parent 下所有直接與間接子附著（含 parent
//     自身若也是別人的子），不留失聯附著。
//
// 相位 1 硬約束：純記憶體 / 純佈局邏輯，無真實視窗 / 繪圖 API、無 `#ifdef` / `win32` / `cocoa`。
// 無效附著（自附、循環附著）一律結構化報錯（`AnchorStatus::Invalid`），不靜默、不崩潰。
#ifndef DS_KERNEL_E1_11_SUBORDINATE_LAYOUT_HPP
#define DS_KERNEL_E1_11_SUBORDINATE_LAYOUT_HPP

#include <cstddef>
#include <vector>

#include "anchor_model.hpp"  // E1-07（上游，可讀不可改）：Anchor / AnchorSpec / Size /
                              // ResolvedPlacement / AnchorStatus / resolve()
#include "null_backend.hpp"  // E1-24（經 E1-07 傳遞，可讀不可改）：具名 SurfaceId 模型

namespace ds::kernel {

// ---------------------------------------------------------------------------
// SubordinateLayout —— 從屬 surface 的父子附著 + 相對定位服務。
//
// 以具名 `SurfaceId` 配對「子 → (父, 定位規格)」記錄（風格對齊 E1-07 `AnchorLayout` /
// E1-03 `AlphaSurfaceService` 的具名鍵記錄配對）。純記憶體、純佈局，不觸碰後端 / OS。
// ---------------------------------------------------------------------------
class SubordinateLayout {
public:
    // 建立（或就地更新）一筆附著：child 以 spec（E1-07 具名 anchor + 相對偏移）相對 parent 定位。
    //   - child / parent 為空、child == parent（自附）、或此附著會造成**循環**（parent 沿既有
    //     附著鏈往上追溯會繞回 child）→ Invalid，不記錄 / 不變更既有記錄。
    //   - spec 本身無效（anchor 越界 / offset 非有限，同 E1-07 判定）→ Invalid。
    //   - 成功：同 child 再次呼叫視為**就地更新**（可換 parent、可換 spec），不新增第二筆。
    AnchorStatus attach(const SurfaceId& child, const SurfaceId& parent, const AnchorSpec& spec);

    // 移除單一附著（僅該 child 自身；不連帶其子孫——連帶清除見 close_parent）。
    // 未知 child → Invalid（不崩潰）；成功 → Ok。
    AnchorStatus detach(const SurfaceId& child);

    // 更新既有附著的定位規格，**不改變父子關係**（parent 不變）。
    // 未知 child、或 spec 無效 → Invalid（不變更既有記錄）；成功 → Ok。
    AnchorStatus reposition(const SurfaceId& child, const AnchorSpec& spec);

    // 父 surface 關閉 / 移除：連帶清除 id 自身的附著記錄（若存在）與其下所有直接、間接子附著
    // （遞迴）。不留孤兒附著。回傳實際清除的附著筆數（可為 0，例如 id 從未出現於任何附著）。
    std::size_t close_parent(const SurfaceId& id);

    // 在給定「父已解析矩形」與「子元件尺寸」下，解析某具名子 surface 的絕對佈局。
    //   - 未知 child → Invalid（不寫 out）。
    //   - parent_placement / child_element 非有限（同 E1-07 `resolve()` 判定）→ Invalid（不寫 out）。
    //   - 成功 → Ok：out 為呼叫端座標系下的絕對矩形——先以 E1-07 `resolve()` 在
    //     `{parent_placement.width, parent_placement.height}` 局部座標系內解析子的位置，
    //     再平移 (+parent_placement.x, +parent_placement.y)。
    // 不快取結果：父矩形改變時，重新呼叫本方法即得子的新位置（「父移動子跟隨」的實作方式）。
    AnchorStatus resolve_child(const SurfaceId& child, const ResolvedPlacement& parent_placement,
                               const Size& child_element, ResolvedPlacement& out) const;

    // --- 查詢（唯讀，不需能力閘控）---
    // 該具名 child 是否已有附著記錄。
    bool is_attached(const SurfaceId& child) const { return find(child) != nullptr; }
    // 目前登錄的附著筆數。
    std::size_t attachment_count() const noexcept { return records_.size(); }
    // 查詢某 child 目前附著的 parent id；未知 child → nullptr。指標於該筆存活期間有效。
    const SurfaceId* parent_of(const SurfaceId& child) const;
    // 查詢某 child 目前的定位規格；未知 child → nullptr。指標於該筆存活期間有效。
    const AnchorSpec* spec_of(const SurfaceId& child) const;
    // 該 id 目前是否至少有一個直接子附著（is a parent of something）。
    bool has_children(const SurfaceId& id) const;

private:
    // 以具名鍵配對記錄（順序即附著順序，永不以數字 index 對外暴露）。
    struct Record {
        SurfaceId child;
        SurfaceId parent;
        AnchorSpec spec;
    };
    Record* find(const SurfaceId& child);
    const Record* find(const SurfaceId& child) const;

    // 檢查「child 附著到 parent」是否會造成循環（含自附）：從 parent 沿既有附著鏈往上追溯
    // （反覆取 parent_of），若曾經抵達 child，即為循環。追溯步數以目前記錄數 + 1 為界，
    // 防禦既有資料異常（理論上不應發生，因每次 attach 皆先過此檢查）導致的無限迴圈。
    bool would_cycle(const SurfaceId& child, const SurfaceId& parent) const;

    std::vector<Record> records_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_11_SUBORDINATE_LAYOUT_HPP
