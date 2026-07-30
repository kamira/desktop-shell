// E4-27 綁定式合成（著替） — module 層 / 子系統 elements
//
// 語意：**綁定式合成（dress-up 換裝合成）**——把多個可切換部件（如底圖 + 服裝 + 配件）
// 依**具名綁定關係**合成為單一視覺：先宣告一組**合成槽**（slot，如 "base" / "outfit" /
// "accessory"），每槽定義一份可切換的候選部件集（part），再把每槽**綁定**到其中一個候選
// 部件；切換綁定值（rebind）即完成「著替」——同一組槽位、換一批部件，重新合成。
//
// 本單元**基於 E4-08** `ds::render::LayerCompositor` 的合成能力：`compose()` 時，依**槽
// 定義順序**（具名順序，非數字 z-order）把目前已綁定槽位的部件對應 surface 逐一交給一個
// 內部暫用的 `LayerCompositor` 做 `add_layer`，最終回傳其 `compose()` 產出的
// `ds::render::CompositionPlan`——不重造合成 / 定址存在性判斷，直接沿用 E4-08（進而沿用
// E4-06 `SurfaceSwitcher::has()`）。本單元只加「具名槽位 -> 目前綁定哪個具名部件」這一層
// 綁定語意，不做真實像素合成。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - 槽位以**具名** `SlotId`（字串）識別，部件以**具名** `PartId`（字串）識別，皆非數字
//     handle / index。
//   - 疊放順序 = **槽定義順序**（`define_slot` 呼叫先後），對外只暴露 `slot_order()` 這種
//     具名序列；沒有「設定第 N 槽」這種數字操作。
//   - 混合模式沿用上游 E4-08 `BlendMode`（具名列舉），透明度沿用其正規化比例 [0,1]。
//
// 不靜默失敗：
//   - `define_slot`：空槽名、空候選部件集、槽名重複定義、候選部件內含空 part_id / 空
//     surface_id、同槽內 part_id 重複 → `BindStatus::Invalid`（不套用、不部分套用）。
//   - `bind` / `rebind`：槽未定義、或 part_id 不在該槽候選集內 → `BindStatus::NotFound`；
//     空 part_id → `BindStatus::Invalid`。`rebind` 額外要求槽**必須已有既存綁定**（代表
//     「正在著替、有舊值可換」）——尚未綁定過的槽呼叫 `rebind` → `BindStatus::NotFound`
//     （請先 `bind`）。
//   - `compose()`：未綁定的槽**明確跳過**（不貢獻任何層，這是文件化的行為，非靜默丟資料）；
//     已綁定槽若其部件對應的 surface 未經 E4-08/E4-06 定址存在，直接回傳 E4-08 給的
//     `CompositeStatus`（`NotFound` / `Invalid`），並**不**回傳部分合成的 plan。
//
// 相位 1 平台中立：純記憶體邏輯 + 描述產出（透過 E4-08），無 `#ifdef` / win32 / cocoa /
// 任何真實繪圖或 OS API、不做真實像素合成。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_27_BOUND_COMPOSITE_HPP
#define DS_ELEMENTS_E4_27_BOUND_COMPOSITE_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "layer_compositor.hpp"  // E4-08（上游，可讀不可改）：LayerCompositor / BlendMode /
                                  // CompositeStatus / CompositionPlan；並透過其 include 鏈傳遞
                                  // SurfaceSwitcher（E4-06）與 ds::kernel::SurfaceId。

namespace ds::elements {

// 操作結果碼 —— 與同子系統各單元同精神：明確、不靜默。
enum class BindStatus {
    Ok,        // 操作成功
    Invalid,   // 前置條件不合法：空槽名 / 空候選集 / 槽重複定義 / 候選部件格式不合法 /
               // 同槽內 part_id 重複 / bind·rebind 的空 part_id
    NotFound,  // bind/rebind：槽未定義，或 part_id 不在該槽候選集內；rebind：槽尚未有既存綁定
};

using SlotId = std::string;
using PartId = std::string;

// 單一**候選部件**描述 —— 純資料。`part_id` 是本部件在其所屬槽內的具名識別；`surface_id`
// 是本部件目前對應的具名 surface（供 E4-08 合成使用，須先經 E4-06 定址存在，於 `compose()`
// 時檢查，而非 `define_slot`/`bind` 時——見檔首「不靜默失敗」段落）。
struct PartOption {
    PartId part_id;
    ds::kernel::SurfaceId surface_id;
    ds::render::BlendMode blend_mode = ds::render::BlendMode::Normal;
    float opacity = 1.0f;
};

// `compose()` 的產出 —— 包裝 E4-08 的 `CompositeStatus` + `CompositionPlan`。`status`
// 非 `Ok` 時 `plan` 為預設空值（不回傳部分合成結果）。
struct CompositeResult {
    ds::render::CompositeStatus status = ds::render::CompositeStatus::Ok;
    ds::render::CompositionPlan plan;
};

// ---------------------------------------------------------------------------
// BoundComposite —— 綁定式合成（著替）的槽位管理器 + 合成計畫產出器。
//
// 以參考持有一個 `SurfaceSwitcher`（不取得所有權；須存活於本物件之外的生命週期內，與
// E4-08 `LayerCompositor` 同慣例），管理一組**依定義順序**排列的具名槽位：每槽一份候選
// 部件集 + 目前綁定哪個候選（可為未綁定）。純記憶體狀態，`compose()` 時才建立一個暫用的
// `ds::render::LayerCompositor` 完成實際合成計畫產出——每次 `compose()` 皆為一次**全新、
// 單次求值**的純查詢，不在物件內部累積跨呼叫的合成器狀態，因此 `bind`/`rebind` 後直接
// 重新 `compose()` 即能拿到反映最新綁定的計畫，不需要額外的「重新合成」步驟。
// ---------------------------------------------------------------------------
class BoundComposite {
public:
    // 綁定一個 `SurfaceSwitcher`，作為 `compose()` 時內部 `LayerCompositor` 的定址存在性
    // 來源（E4-08 -> E4-06）。
    explicit BoundComposite(ds::render::SurfaceSwitcher& switcher) : switcher_(switcher) {}

    // --- 定義槽位 ---
    // 定義一個具名合成槽 + 其候選部件集。
    //   - `slot` 為空 → `Invalid`（不定義）。
    //   - `parts` 為空 → `Invalid`（一個槽至少要有一個候選部件才有意義）。
    //   - `slot` 已定義過 → `Invalid`（不覆蓋既有定義；本相位不提供重新定義 / 移除槽 API）。
    //   - `parts` 內任一 `part_id` 或 `surface_id` 為空 → `Invalid`（不套用）。
    //   - `parts` 內 `part_id` 重複 → `Invalid`（同槽內部件識別須唯一，不套用）。
    // 成功 → `Ok`；槽以**呼叫順序**加入（`slot_order()`/`compose()` 依此順序），新槽初始
    // 為**未綁定**狀態。
    BindStatus define_slot(const SlotId& slot, const std::vector<PartOption>& parts);

    // --- 綁定 / 著替 ---
    // 綁定（或改綁）一個槽到其候選集中的某個具名部件。
    //   - 槽未定義 → `NotFound`。
    //   - `part_id` 為空 → `Invalid`。
    //   - `part_id` 不在該槽候選集內 → `NotFound`（「無效 part」，不套用）。
    // 成功 → `Ok`；無論該槽先前是否已綁定，皆以此次呼叫的 part_id 為準（初次綁定與改綁
    // 共用同一入口）。
    BindStatus bind(const SlotId& slot, const PartId& part_id);
    // 著替：把一個**已有既存綁定**的槽切換到另一個候選部件（語意上是「換裝」，而非「首次
    // 穿上」）。
    //   - 槽未定義 → `NotFound`。
    //   - 槽**尚未**有既存綁定（從未成功 `bind` 過）→ `NotFound`（沒有舊值可換；請先
    //     `bind`）。
    //   - `part_id` 為空 → `Invalid`。
    //   - `part_id` 不在該槽候選集內 → `NotFound`（不套用，維持原綁定）。
    // 成功 → `Ok`；下一次 `compose()` 即反映新綁定（重新合成）。
    BindStatus rebind(const SlotId& slot, const PartId& part_id);

    // --- 查詢 ---
    bool has_slot(const SlotId& slot) const;
    // 該槽目前是否已有綁定（尚未 `bind` 過的槽回 false）。
    bool is_bound(const SlotId& slot) const;
    // 該槽目前綁定的具名部件 id；槽未定義或尚未綁定 → 空字串（明確，不回傳殘留 / 任意值）。
    PartId current_part(const SlotId& slot) const;
    // 依**定義順序**列舉槽位（具名清單；順序僅表示定義先後，非 z-order，NFR-02）。
    std::vector<SlotId> slot_order() const;
    // 目前已定義的槽位數。
    std::size_t slot_count() const noexcept { return slots_.size(); }

    // --- 合成 ---
    // 依槽**定義順序**，把每個**已綁定**槽的目前部件對應 surface 交給一個暫用的 E4-08
    // `LayerCompositor` 做 `add_layer`（混合模式 / 透明度取自該部件），再回傳其
    // `compose()` 產出的 `CompositionPlan`。
    //   - 未綁定的槽**明確跳過**（不貢獻任何層；文件化行為，非靜默丟資料）。
    //   - 任一已綁定槽的 `add_layer` 未回 `Ok`（如對應 surface 未經 E4-06 定址存在 →
    //     `NotFound`）→ 立即回傳該 `CompositeStatus`，`plan` 維持預設空值（不回傳部分
    //     合成結果）。
    //   - 全部已綁定槽皆成功加入 → `CompositeStatus::Ok` + 完整 `CompositionPlan`。
    CompositeResult compose() const;

private:
    struct SlotEntry {
        SlotId slot_id;
        std::vector<PartOption> parts;
        PartId bound_part;  // 空 = 未綁定
    };

    ds::render::SurfaceSwitcher& switcher_;  // E4-08 LayerCompositor 的定址存在性來源（不持有所有權）
    std::vector<SlotEntry> slots_;           // 依定義順序排列，非數字索引語意

    std::vector<SlotEntry>::iterator find_slot(const SlotId& slot);
    std::vector<SlotEntry>::const_iterator find_slot(const SlotId& slot) const;
    static const PartOption* find_part(const SlotEntry& entry, const PartId& part_id);
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_27_BOUND_COMPOSITE_HPP
