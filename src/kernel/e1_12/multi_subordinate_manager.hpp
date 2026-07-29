// E1-12 多重從屬 surface 並存 — 一個父可同時掛多個子(從屬) surface 的管理層
// （platform 相位 1 = Mac / null 期）
//
// 語意：一個父 surface 可**同時**掛多個從屬（子）surface——例如同一個輸入框同時有
// tooltip、下拉選單、驗證提示三個子並存——各自以 E1-11 `SubordinateLayout` 定位、
// **獨立管理**（互不覆寫彼此的附著記錄）、**父移動時全部跟隨**（因為每個子的絕對佈局
// 都是重新解析而得，非快取座標）、可**個別關閉**（只收掉其中一個子）或**全部關閉**
// （父關閉時連帶清光底下所有子），並避免衝突（多個子各自以自己的附著記錄存在，不會
// 因為「同一 parent 掛第二個 child」而互相覆蓋——這正是 E1-11 `attach()` 只有「同一
// child 再次呼叫才視為就地更新」的既有語意，本單元在其上加一層「列舉某 parent 底下
// 全部子」與「批次操作」的管理便利）。
//
// 建於上游 E1-11 `SubordinateLayout` 之上：本單元**不重新發明**父子附著記錄或相對定位
// 計算，而是以組合（composition）方式持有一個 `SubordinateLayout` 實例作為唯一真實
// 來源（single source of truth），加一層「多子並存」的管理 API：
//   - `attach_child` / `detach_child`：單一子的附著 / 解除，直接委由 E1-11 `attach` /
//     `detach`。
//   - `children_of`：列舉某 parent 目前所有直接子（依附著順序，具名回傳，不以數字
//     index 對外暴露）。
//   - `reposition_all`：給定 parent 的新已解析矩形，**一次性**重新解析其下所有直接子的
//     絕對佈局（「父移動時全部跟隨」的批次版本；底層本就不快取座標，本方法只是把
//     「對每個子呼叫一次 `resolve_child`」包成一個便利入口）。
//   - `detach_all`：委由 E1-11 `close_parent`，一次關閉 parent 底下所有子（含巢狀子孫）。
//
// **元件尺寸的記錄**：E1-11 `resolve_child` 每次呼叫都需要呼叫端提供「子元件尺寸」；
// 若要讓 `reposition_all(parent, new_placement)` 只憑一個新的父矩形就能批次重新解析
// *全部*子，本管理層必須額外記住每個子的元件尺寸（E1-11 本身不記，因為它的
// `resolve_child` 設計成每次呼叫獨立傳入）。此記錄與 E1-11 的附著記錄保持同步：子
// 被 detach（無論透過 `detach_child` 或 `detach_all` 連帶清除）時，對應的元件尺寸
// 記錄也一併清除，不留孤兒資料。
//
// 相位 1 硬約束：純記憶體 / 純管理邏輯，無真實視窗 / 繪圖 API、無 `#ifdef` / `win32` /
// `cocoa`。無效附著（自附、循環、非有限元件尺寸）一律結構化報錯，不靜默、不崩潰。
#ifndef DS_KERNEL_E1_12_MULTI_SUBORDINATE_MANAGER_HPP
#define DS_KERNEL_E1_12_MULTI_SUBORDINATE_MANAGER_HPP

#include <cstddef>
#include <utility>
#include <vector>

#include "subordinate_layout.hpp"  // E1-11（上游，可讀不可改）：SubordinateLayout /
                                    // AnchorSpec / Size / ResolvedPlacement / AnchorStatus /
                                    // SurfaceId（經 E1-07 傳遞）

namespace ds::kernel {

// ---------------------------------------------------------------------------
// MultiSubordinateManager —— 多重從屬 surface 並存的管理層。
//
// 以組合方式持有一個 `SubordinateLayout`（E1-11）作為父子附著 + 相對定位的唯一真實
// 來源；本類別只負責「同一 parent 下多個 child 並存」的列舉與批次操作，不重複記錄
// 附著關係本身。
// ---------------------------------------------------------------------------
class MultiSubordinateManager {
public:
    // 附著一個從屬(子) surface 到 parent，並記錄其定位規格與元件尺寸（供 reposition_all
    // 批次重新解析使用）。
    //   - child/parent 空、自附、循環附著、spec 無效 → Invalid（同 E1-11 attach 語意，
    //     不記錄，不變更既有記錄）。
    //   - child_element 非有限或為負 → Invalid（不記錄；與 E1-07 resolve() 對元件尺寸
    //     的判定一致，提前擋下，避免留下事後才發現無法 reposition_all 的附著）。
    //   - 成功：同 child 再次呼叫視為就地更新（可換 parent、spec、元件尺寸），與 E1-11
    //     同一 child 語意一致，不會產生第二筆並存記錄。
    AnchorStatus attach_child(const SurfaceId& parent, const SurfaceId& child,
                               const AnchorSpec& spec, const Size& child_element);

    // 列舉 parent 目前所有直接子（依附著順序；具名回傳，不以數字 index 對外暴露）。
    // parent 不存在或無任何子 → 空 vector。
    std::vector<SurfaceId> children_of(const SurfaceId& parent) const;

    // 給定 parent 的新已解析矩形，批次重新解析其下所有直接子的絕對佈局
    // （「父移動時全部跟隨」）——對每個子以其記錄的元件尺寸呼叫底層 resolve_child。
    // 回傳依附著順序排列的 {child, ResolvedPlacement} 對；parent 無子 → 空 vector。
    // 個別子若解析失敗（理論上不會發生，因 attach_child 已在記錄前驗證過 spec 與元件
    // 尺寸）則略過、不列入回傳（不崩潰，結構化省略優於半寫壞資料）。
    std::vector<std::pair<SurfaceId, ResolvedPlacement>> reposition_all(
        const SurfaceId& parent, const ResolvedPlacement& parent_placement) const;

    // 個別關閉 / 移除單一子附著（不影響同 parent 下其餘子）。委由 E1-11 detach；
    // 未知 child → Invalid（不崩潰）。成功時一併清除該 child 的元件尺寸記錄。
    AnchorStatus detach_child(const SurfaceId& child);

    // 全部關閉：parent 底下所有直接 / 間接子附著一次清除（含巢狀從屬鏈），委由 E1-11
    // close_parent。回傳實際清除的附著筆數（可為 0）。一併清除所有隨之移除之子的元件
    // 尺寸記錄，不留孤兒資料。
    std::size_t detach_all(const SurfaceId& parent);

    // --- 查詢（唯讀，不需能力閘控）---
    // parent 是否至少有一個直接子並存。
    bool has_children(const SurfaceId& parent) const { return layout_.has_children(parent); }
    // parent 目前直接子的數量。
    std::size_t child_count(const SurfaceId& parent) const { return children_of(parent).size(); }
    // 該具名 child 是否已有附著記錄。
    bool is_attached(const SurfaceId& child) const { return layout_.is_attached(child); }
    // 查詢某 child 目前附著的 parent id；未知 child → nullptr。
    const SurfaceId* parent_of(const SurfaceId& child) const { return layout_.parent_of(child); }
    // 查詢某 child 目前的定位規格；未知 child → nullptr。
    const AnchorSpec* spec_of(const SurfaceId& child) const { return layout_.spec_of(child); }
    // 查詢某 child 目前記錄的元件尺寸；未知 child → nullptr。指標於該筆存活期間有效。
    const Size* element_size_of(const SurfaceId& child) const;
    // 目前透過本管理層登錄的附著總筆數（跨所有 parent）。
    std::size_t attachment_count() const noexcept { return layout_.attachment_count(); }

private:
    // 以具名鍵配對每個 child 目前登錄的元件尺寸（順序即附著順序，供 children_of /
    // reposition_all 以穩定順序列舉；永不以數字 index 對外暴露）。與 layout_ 的附著記錄
    // 保持同步：child 被移除時，這裡的對應記錄也一併移除。
    std::vector<std::pair<SurfaceId, Size>> element_sizes_;

    void set_element_size(const SurfaceId& child, const Size& size);
    void erase_element_size(const SurfaceId& child);
    // 移除所有「已不在 layout_ 附著記錄中」的元件尺寸記錄（供 detach_all 連帶清除後同步）。
    void prune_element_sizes();

    SubordinateLayout layout_;  // 委由 E1-11：父子附著記錄 + 相對定位計算的唯一真實來源
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_12_MULTI_SUBORDINATE_MANAGER_HPP
