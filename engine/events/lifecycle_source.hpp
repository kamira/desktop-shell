// E5-03 生命週期事件 — 平台中立介面
//
// surface / profile 的生命週期狀態轉換（created / shown / hidden / activated /
// deactivated / destroyed）的發佈與訂閱。上層（動畫、資源釋放、焦點管理等）藉此
// 得知某個 surface 進入了哪個生命週期相位。
//
// 本單元屬 engine 層（平台中立純邏輯）：
//   - 狀態轉換由呼叫端驅動（`transition(surface, phase)`），**不綁任何真實視窗系統**。
//     真實後端於相位 2 只需在 OS 生命週期回呼到達時呼叫同一個 transition 路徑。
//   - 合法轉換以固定的狀態機表約束（如已 destroyed 不能再 shown）；非法轉換被拒絕、
//     不改狀態、不發事件——因此可完全以單元測試驗證（注入轉換、斷言事件與拒絕）。
#ifndef DS_EVENTS_E5_03_LIFECYCLE_SOURCE_HPP
#define DS_EVENTS_E5_03_LIFECYCLE_SOURCE_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>

namespace ds::events {

// surface / profile 的識別碼。由呼叫端指派；本單元不解讀其意義。
using SurfaceId = std::uint64_t;

// 訂閱代號。由 subscribe() 發出，供 unsubscribe() 使用。0 保留為無效值。
using SubscriptionId = std::uint64_t;

// 生命週期相位。跨平台一致的語意（各平台以自身機制觸發同一種相位）。
enum class LifecyclePhase {
    Created,      // 已建立（尚未顯示）——surface 的初始相位
    Shown,        // 已顯示（可見但未取得焦點）
    Hidden,       // 已隱藏（不可見，但仍存在）
    Activated,    // 已啟用（取得焦點 / 成為前景）
    Deactivated,  // 已停用（失去焦點 / 退居背景，仍可見）
    Destroyed,    // 已銷毀（終端相位，不可再轉出）
};

// 一次生命週期轉換所攜帶的資料。純資料、平台中立。
struct LifecycleEvent {
    SurfaceId surface;      // 發生轉換的 surface
    LifecyclePhase from;    // 轉換前相位
    LifecyclePhase to;      // 轉換後相位
};

// 生命週期事件回呼。訂閱者於轉換分派時被呼叫。
using LifecycleListener = std::function<void(const LifecycleEvent&)>;

// 判斷一個相位轉換是否合法（固定狀態機；不含平台分支）。
//
// 合法邊：
//   Created     -> Shown | Destroyed
//   Shown       -> Activated | Deactivated | Hidden | Destroyed
//   Hidden      -> Shown | Destroyed
//   Activated   -> Deactivated | Hidden | Destroyed
//   Deactivated -> Activated | Shown | Hidden | Destroyed
//   Destroyed   -> （終端，無合法轉出）
// 同相位自我轉換（from == to）一律視為非法（無實質轉換、不發事件）。
bool is_legal_transition(LifecyclePhase from, LifecyclePhase to) noexcept;

// 生命週期來源：追蹤各 surface 的目前相位，驅動並發佈其狀態轉換。
//
// 語意保證：
//   - `create(surface)` 以 Created 相位登記一個新 surface；重複登記為 no-op 回 false。
//   - `transition(surface, to)` 僅在轉換合法時改狀態並對所有訂閱者分派事件；
//     非法轉換（含未登記的 surface、終端 Destroyed 轉出、自我轉換）回 false 且不改狀態。
//   - 轉入 Destroyed 後該 surface 仍保留其相位供查詢，但任何再轉出皆為非法。
//   - 分派前先取訂閱快照：listener 於回呼中訂閱 / 解除訂閱不影響本輪、不破壞疊代。
//   - 多訂閱者依 SubscriptionId 遞增（即訂閱順序）分派，順序穩定可測。
class LifecycleSource {
public:
    LifecycleSource() = default;

    // 訂閱生命週期事件。回傳非 0 訂閱代號；listener 為空時回傳 0（無效訂閱）。
    SubscriptionId subscribe(LifecycleListener listener);

    // 解除訂閱。回傳是否確實移除；未知 id 為 no-op 並回傳 false。
    bool unsubscribe(SubscriptionId id);

    // 以 Created 相位登記一個新 surface。已存在則不動、回傳 false。
    bool create(SurfaceId surface);

    // 驅動一次相位轉換。合法時改狀態、分派事件並回傳 true；否則回傳 false（不改狀態）。
    bool transition(SurfaceId surface, LifecyclePhase to);

    // 查詢某 surface 目前相位；未登記則回傳 false（out 不動）。
    bool phase_of(SurfaceId surface, LifecyclePhase& out) const;

    // 某 surface 是否已登記。
    bool has_surface(SurfaceId surface) const;

    // 目前訂閱者數量。
    std::size_t listener_count() const noexcept { return listeners_.size(); }

    // 目前已登記的 surface 數量。
    std::size_t surface_count() const noexcept { return phases_.size(); }

private:
    // 以有序容器保存以保證分派順序穩定（依 SubscriptionId 遞增即訂閱順序）。
    std::map<SubscriptionId, LifecycleListener> listeners_;
    std::map<SurfaceId, LifecyclePhase> phases_;
    SubscriptionId next_id_ = 1;  // 0 保留為無效
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_03_LIFECYCLE_SOURCE_HPP
