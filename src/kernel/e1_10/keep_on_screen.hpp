// E1-10 保持在螢幕內 — 依 E1-07 anchor 定位結果 + E1-18 具名螢幕範圍夾回可視範圍
// （platform 相位 1 = Mac / null 期）
//
// 語意：元件依 anchor（E1-07）解析出 `ResolvedPlacement` 後，可能因偏移 / 元件尺寸而
// 超出螢幕邊界（例如靠邊緣的彈窗）。本單元提供 **KeepOnScreen** 服務：把一個已解析的
// `ResolvedPlacement` 相對某個具名螢幕（E1-18 `ScreenId`）的可視範圍**夾回（clamp）**
// 到範圍內，避免元件被裁切在螢幕外。
//
// NFR-02：核心 API 不出現硬編絕對座標。夾回計算**不引用任何寫死的螢幕尺寸數字**——
// 螢幕尺寸與 E1-07 `resolve()` 的 container 尺寸同一模式：由呼叫端在**夾回計算的邊界**
// 提供（例如量測得到的螢幕可用範圍），E1-18 本身（`ScreenRegistry` / `Screen`）不攜帶
// 任何像素幾何（其幾何只以具名 `ScreenRole` / `ScreenAnchor` 表達），故本單元透過 E1-18
// 只做**具名螢幕存在性查詢 / 多螢幕情境的鍵**，實際尺寸仍由呼叫端提供，維持與 E1-07 一致
// 的「具體數字只在計算邊界出現」原則。調整量以**相對量**（把矩形移回範圍內所需的位移）
// 表達，不引入新的絕對定位 API。
//
// 相位 1 硬約束：純計算邏輯，無真實視窗 / 螢幕查詢 API、無 `#ifdef` / `win32` / `cocoa`。
// 無效輸入（非有限 / 負值尺寸、未知具名螢幕）一律結構化回報（`Invalid`），**不靜默**。
#ifndef DS_KERNEL_E1_10_KEEP_ON_SCREEN_HPP
#define DS_KERNEL_E1_10_KEEP_ON_SCREEN_HPP

#include "anchor_model.hpp"    // E1-07（可讀不可改）：Size / ResolvedPlacement / is_finite_size
#include "named_screens.hpp"   // E1-18（可讀不可改）：ScreenId / ScreenRegistry

namespace ds::kernel {

// 操作結果碼 —— 與 E1-07 `AnchorStatus` / E1-18 同風格（平台中立、結構化、不靜默）。
enum class KeepOnScreenStatus {
    Ok,       // 計算成功（可能是「本已在螢幕內，未調整」或「已推回範圍內」）
    Invalid,  // 前置條件不滿足（非有限 / 負值尺寸、未知具名螢幕等）
};

// 該 placement 是否已完全落在 [0, screen.width] x [0, screen.height] 範圍內。
// screen 非有限 / 負值尺寸 → 保守回 false。
bool is_within_screen(const ResolvedPlacement& placement, const Size& screen);

// 純計算：把 `placement` 相對 `screen`（可視範圍尺寸，左上角為原點，同 E1-07 容器座標系）
// 夾回範圍內，結果寫入 `out`。
//
// 規則：
//   - width / height 維持不變（本單元只調整位置，不縮放元件）。
//   - x 夾至 [0, max(0, screen.width  - placement.width)]；y 同理套用 height。
//     這同時涵蓋「超出右 / 下邊界 → 推回」「超出左 / 上邊界 → 推回」與「完全在內 → 不動」。
//   - 元件尺寸 **大於** 螢幕尺寸（該軸 max(0, screen - element) = 0）時，該軸夾至 0
//     （貼齊螢幕起邊）——已知不可能完全容納時，選擇不裁切、不縮放，僅以起邊對齊呈現最多內容。
//   - 角落同時超出（例如右下角超出）→ 兩軸各自獨立夾回，等效同時處理。
//
// 非有限 / 負值 placement 尺寸、非有限 / 負值 screen 尺寸 → `Invalid`，不寫 `out`（不靜默）。
// placement.x / y 本身允許任意有限值（含負值 —— 這正是「超出左 / 上邊界」的輸入態樣）。
KeepOnScreenStatus constrain(const ResolvedPlacement& placement, const Size& screen,
                             ResolvedPlacement& out);

// ---------------------------------------------------------------------------
// KeepOnScreen —— 綁定 E1-18 具名螢幕拓撲的夾回服務。
//
// E1-18 的 `ScreenRegistry` 不攜帶像素幾何（幾何只以具名角色 / 相對錨點表達），故本服務
// 以 registry 做**具名螢幕存在性查詢**（多螢幕情境下用具名 `ScreenId` 而非數字 index 指涉
// 「要夾回哪一個螢幕」），實際尺寸仍由呼叫端於呼叫當下提供（與 E1-07 `resolve()` 的
// container 尺寸同一模式）。未知具名螢幕 → `Invalid`（不猜測、不靜默套用預設尺寸）。
// ---------------------------------------------------------------------------
class KeepOnScreen {
public:
    // registry 於本物件存活期間須有效（僅持有參照，不複製；與 AnchorLayout 對上游模型的
    // 使用風格一致）。
    explicit KeepOnScreen(const ScreenRegistry& registry) : registry_(&registry) {}

    // 把 `placement` 夾回具名螢幕 `screen_id` 的可視範圍（`screen_size` 為該螢幕的可用尺寸，
    // 由呼叫端量測 / 提供）內。
    //   - `screen_id` 不在 registry 內（`is_known` 為 false）→ `Invalid`（不猜測其尺寸）。
    //   - 其餘錯誤語意同自由函式 `constrain()`。
    // 成功 → `Ok`，`out` 填入夾回後的 placement。
    KeepOnScreenStatus constrain_on(const ScreenId& screen_id, const Size& screen_size,
                                     const ResolvedPlacement& placement,
                                     ResolvedPlacement& out) const;

    // 目前綁定的具名螢幕拓撲是否已知曉該具名螢幕（便於呼叫端在夾回前先行判斷）。
    bool knows_screen(const ScreenId& screen_id) const { return registry_->is_known(screen_id); }

private:
    const ScreenRegistry* registry_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_10_KEEP_ON_SCREEN_HPP
