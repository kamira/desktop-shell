// E1-08 自由拖曳與位置記憶 — surface 拖曳狀態機 + 位置持久化（platform 相位 1 = Mac / null 期）
//
// 語意：**自由拖曳 surface 並記憶其位置**——使用者可把一個具名 surface 拖曳到任意位置，
// 放開後記住該位置、下次還原。本單元把三件事組合起來：
//   1. **拖曳狀態機**：`begin_drag` → `drag_to`（一次或多次）→ `end_drag`（提交）/ `cancel_drag`
//      （放棄）。相位 1 平台中立——沒有真實視窗拖曳 API，拖曳事件以**注入式**呼叫表達，
//      目標位置以宣告式 `AnchorSpec`（承 E1-07）承載。
//   2. **位置以 anchor / 相對表達**（NFR-02）：位置一律是「九宮具名錨點 + 容器尺寸正規化偏移」，
//      **不出現絕對像素座標 / 數字 z-order**。committed（記憶）位置以具名 `SurfaceId` 為鍵保存，
//      可經 E1-07 `resolve` 在給定容器 / 元件尺寸下落地為具體佈局。
//   3. **位置記憶（持久化）**：`serialize_positions()` 以 E7-12 設定值寫回把所有記憶位置序列化為
//      E7-01 宣告式文字格式；`load_positions()` 反向還原。存/取即「下次還原」的落地。
//
// 上游相依（已合併，可讀不可改）：
//   - E1-03 逐像素 alpha surface → 傳遞取得 E1-24 `KernelBackend` / `SurfaceId` surface 模型；
//     拖曳操作以 `backend.has_surface(id)` 閘控「只拖曳真實存在的 surface」。
//   - E1-07 anchor 定位模型 → `Anchor` / `AnchorSpec` / `Offset` / `resolve` / `is_valid_anchor`：
//     位置的宣告式表達與落地（NFR-02 正解）。
//   - E7-12 設定值寫回（其上為 E7-01 格式核心）→ 位置記憶的序列化 / 反序列化（設定持久化）。
//
// 相位 1 硬約束：無 `#ifdef` / `win32` / `cocoa` / 真實視窗 / 繪圖 API；純狀態機 + 純資料。
// 無效拖曳（未知 / 未註冊 surface、非拖曳中操作、重複 begin）與無效位置（越界 anchor / 非有限
// offset）一律結構化回報，**不靜默、不崩潰**。
#ifndef DS_KERNEL_E1_08_DRAGGABLE_SURFACE_HPP
#define DS_KERNEL_E1_08_DRAGGABLE_SURFACE_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "anchor_model.hpp"  // E1-07（可讀不可改）：Anchor / AnchorSpec / Offset / resolve / Size
#include "null_backend.hpp"  // E1-24（經 E1-03 傳遞，可讀不可改）：KernelBackend / SurfaceId

namespace ds::kernel {

// 拖曳 / 位置操作結果碼 —— 結構化、平台中立、不靜默（與 E1-03 `AlphaStatus` /
// E1-07 `AnchorStatus` 同風格，另加拖曳狀態機專屬碼以精確回報，而非一律 Invalid）。
enum class DragStatus {
    Ok,               // 操作成功
    Invalid,          // 前置條件不滿足：空 / 未知（後端無此）/ 未註冊 surface、無效 AnchorSpec、
                      // 持久化文字無法解析等
    NotDragging,      // 該操作需要一個進行中的拖曳，但此 surface 目前未在拖曳
    AlreadyDragging,  // 對一個已在拖曳中的 surface 再次 begin_drag
};

// 九宮具名錨點 ↔ 穩定字串名稱（供位置記憶持久化；NFR-02：以**具名角色**存取，非數字）。
// 名稱採小寫連字號："top-left" / "center" / "bottom-right" …（與九宮列舉一一對應）。
const char* anchor_to_name(Anchor a);
// 反解析：未知 / 不合法名稱回 false 且不觸碰 out（不靜默）。
bool anchor_from_name(const std::string& name, Anchor& out);

// ---------------------------------------------------------------------------
// DraggableSurface —— 自由拖曳 + 位置記憶服務層。
//
// 以參考持有任一 `KernelBackend`（相位 1 為 `NullKernelBackend`；真實後端上線後同介面即可）。
// 內部以具名 `SurfaceId` 為鍵保存每個 surface 的**已提交（記憶）位置**（committed AnchorSpec，
// 承 E1-07 宣告式模型），另維護進行中的拖曳狀態（pending 目標位置 + 起始位置，供 cancel 還原）。
// committed 位置即「記憶位置」——`end_drag` 把 pending 提交為 committed，`serialize_positions`
// 把它們寫回設定。
//
// 位置全程以宣告式 `AnchorSpec`（九宮錨點 + 正規化偏移）承載，無絕對像素座標（NFR-02）；
// 具體像素只在 `resolve_live` 這個純佈局計算邊界、由呼叫端提供的容器 / 元件尺寸推導而得。
// 記錄以具名鍵線性配對（順序即建立順序，永不以數字 index 對外暴露；風格對齊 E1-03
// `AlphaSurfaceService` / E1-07 `AnchorLayout`）。
// ---------------------------------------------------------------------------
class DraggableSurface {
public:
    // 綁定一個後端（不取得所有權；後端須存活於本服務之外的生命週期內）。
    explicit DraggableSurface(KernelBackend& backend) : backend_(backend) {}

    // --- 註冊 / 更新位置（拖曳前提）---
    // 指派 / 更新某具名 surface 的位置（committed）。這是「這個 surface 可被拖曳、其初始 /
    // 目前位置在此」的登錄入口。
    //   - id 為空、後端無此 surface（!has_surface）、AnchorSpec 無效（越界 anchor / 非有限
    //     offset）→ Invalid（不記錄）。
    //   - 該 surface 正在拖曳中 → Invalid（拖曳期間不可外部改位置，避免與 pending 競態）。
    //   - 成功 → Ok（同 id 再次指派為就地更新）。
    DragStatus set_position(const SurfaceId& id, const AnchorSpec& spec);
    // 解除註冊（同時移除記憶位置與任何進行中的拖曳狀態）。未知 id 回 Invalid（不崩潰）。
    DragStatus forget(const SurfaceId& id);

    // 該具名 surface 是否已註冊（有記憶位置）。
    bool is_tracked(const SurfaceId& id) const { return find(id) != nullptr; }
    // 目前登錄（有記憶位置）的 surface 數量。
    std::size_t tracked_count() const noexcept { return positions_.size(); }

    // --- 拖曳狀態機（注入式事件；相位 1 無真實視窗拖曳 API）---
    // 開始拖曳某已註冊 surface。
    //   - 未註冊（無記憶位置）/ 後端無此 surface / 空 id → Invalid。
    //   - 已在拖曳中 → AlreadyDragging。
    //   - 成功 → Ok（記錄起始位置 = 目前 committed；pending 初始化為起始位置）。
    DragStatus begin_drag(const SurfaceId& id);
    // 把拖曳中的 surface 移到新的目標位置（可於一次拖曳內多次呼叫；只更新 pending，未提交）。
    //   - 該 surface 未在拖曳 → NotDragging。
    //   - AnchorSpec 無效（越界 anchor / 非有限 offset）→ Invalid（不更新 pending，不靜默）。
    //   - 成功 → Ok。
    DragStatus drag_to(const SurfaceId& id, const AnchorSpec& spec);
    // 結束拖曳並**提交** pending 為記憶位置（committed）。未在拖曳 → NotDragging；成功 → Ok。
    // 提交後 `remembered_position` 反映拖曳到的新位置（即「放開後記住」）。
    DragStatus end_drag(const SurfaceId& id);
    // 取消拖曳並**放棄** pending（committed 位置不變，等同還原到拖曳前）。未在拖曳 → NotDragging。
    DragStatus cancel_drag(const SurfaceId& id);

    // 該具名 surface 目前是否正在拖曳。
    bool is_dragging(const SurfaceId& id) const { return find_drag(id) != nullptr; }
    // 目前進行中的拖曳數量。
    std::size_t dragging_count() const noexcept { return drags_.size(); }

    // --- 位置查詢 ---
    // 記憶位置（committed）——「放開後記住、下次還原」的那個位置。未註冊回 nullptr。
    // 拖曳進行中仍回上一次提交的位置（pending 尚未成為記憶）。
    const AnchorSpec* remembered_position(const SurfaceId& id) const {
        const Record* r = find(id);
        return r ? &r->spec : nullptr;
    }
    // 目前實時位置（live）：拖曳中回 pending 目標位置，否則回 committed 記憶位置。
    // 未註冊且未拖曳回 nullptr。表達「此刻視覺上在哪」。
    const AnchorSpec* live_position(const SurfaceId& id) const;

    // 把某 surface 的**實時位置**在給定容器 / 元件尺寸下解析為具體佈局（委由 E1-07 resolve）。
    // 未註冊且未拖曳 → Invalid；其餘錯誤語意同 `resolve`（非有限 / 負尺寸 → Invalid）。
    AnchorStatus resolve_live(const SurfaceId& id, const Size& container, const Size& element,
                              ResolvedPlacement& out) const;

    // --- 位置記憶持久化（經 E7-12 設定值寫回 → E7-01 文字格式）---
    // 把所有記憶位置（committed）序列化為 E7-01 宣告式文字（首行 format_version）。
    // 每個 surface 一個 map 條目：`{ anchor: <具名>, dx: <正規化>, dy: <正規化> }`。
    // 輸出即 `load_positions` / E7-01 `parse` 能再讀回的合法輸入（round-trip）。
    std::string serialize_positions() const;
    // 從 E7-01 文字還原記憶位置（設定持久化的「取」/「下次還原」）。
    //   - 文字無法解析、條目結構不符（缺 anchor / dx / dy、型別錯、anchor 名無效）→ Invalid
    //     且**不套用任何條目**（全有或全無，不靜默、不留半套狀態）。
    //   - 成功 → Ok，記憶位置以還原內容覆寫（同名 id 就地更新）。
    // 注意：還原是純資料回填，**不**經後端 has_surface 閘控——記憶可在對應 surface 建立前先載入；
    // 對真實 surface 生效發生在後續（backend 閘控的）拖曳 / set_position 時。
    DragStatus load_positions(const std::string& text);

private:
    // 記憶位置記錄 —— 具名鍵 + 宣告式位置，純資料，無平台 handle。
    struct Record {
        SurfaceId id;
        AnchorSpec spec;
    };
    Record* find(const SurfaceId& id);
    const Record* find(const SurfaceId& id) const;

    // 進行中的拖曳狀態 —— 純資料，無平台 handle。
    struct DragState {
        SurfaceId id;
        AnchorSpec start;    // 拖曳開始時的 committed 位置（cancel 用；committed 全程不變故亦即還原值）
        AnchorSpec pending;  // 目前拖曳目標（drag_to 更新；end_drag 提交為 committed）
    };
    DragState* find_drag(const SurfaceId& id);
    const DragState* find_drag(const SurfaceId& id) const;

    KernelBackend& backend_;
    std::vector<Record> positions_;   // committed（記憶）位置，以具名 SurfaceId 為鍵（承 E1-07 模型）
    std::vector<DragState> drags_;     // 進行中的拖曳（順序即開始順序，永不以數字 index 對外暴露）
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_08_DRAGGABLE_SURFACE_HPP
