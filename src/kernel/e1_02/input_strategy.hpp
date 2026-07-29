// E1-02 輸入策略四態 — 平台中立介面（相位 1 = null）
//
// surface 的**輸入處理策略**只有四種具名狀態。四個產品（角色 / 面板 / 啟動器 / 浮層）
// 共用同一 surface kernel，差異之一即「這個 surface 如何參與輸入」。本單元把該差異
// 收斂成四個**具名**狀態（NFR-02：具名，非數字），並定義各態的命中（hit-test）與
// 事件路由（routing）行為、狀態轉換、預設策略，以及與 E1-24 K3 輸入原語的整合。
//
// 建於上游 E1-24（`KernelBackend` / `InputPolicy` / `InputEvent` / `SurfaceId`）之上：
//   - E1-24 的 K3 原語 `InputPolicy` 只有三態（Modal / Accepting / PassThrough），
//     是後端層「這個 surface 吃不吃輸入 / 獨不獨占」的低階旋鈕。
//   - E1-02 的 `InputStrategy` 是四態的**產品語意層**，多出一個 `Inert`（實心命中但
//     吞掉輸入、不下傳）——後端三態無法直接表達的態。E1-02 提供 `to_backend_policy()`
//     把四態對齊 / 下推到 E1-24 三態，並在路由層補足後端表達不了的細節。
//
// 相位 1（Mac / null 期）約束：只有介面 + null 後端記憶體狀態；不含任何平台分支
// （無 `#ifdef` / win32 / cocoa）、不觸真實 OS。能力閘控一律經 `has()`（NFR-03）。
#ifndef DS_KERNEL_E1_02_INPUT_STRATEGY_HPP
#define DS_KERNEL_E1_02_INPUT_STRATEGY_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "null_backend.hpp"  // E1-24（上游，可讀不可改）：KernelBackend / InputPolicy / InputEvent / SurfaceId

namespace ds::kernel {

// ---------------------------------------------------------------------------
// 輸入策略四態 —— surface 的輸入處理策略。具名（NFR-02），非數字。
//
// 四態彼此正交，涵蓋「不搶焦點的面板」「獨占焦點的 modal」「點擊穿透的桌布 / 裝飾層」
// 「顯示但完全不參與輸入的擺設」四種產品需求。
// ---------------------------------------------------------------------------
enum class InputStrategy {
    Interactive,   // 正常接收輸入：命中實心、事件遞送本 surface。
    Capture,       // 獨占捕捉：所有輸入強制導向本 surface（modal 抓取），無視命中目標。
    ClickThrough,  // 點擊穿透：命中透明、事件下傳到其下 surface，本 surface 不吃輸入。
    Inert,         // 不接收（停用）：命中實心、但吞掉事件、不下傳、本 surface 亦不處理。
};

// 命中測試結果 —— 具名，非座標。
enum class HitResult {
    Solid,        // 命中落在本 surface（實心）。
    Transparent,  // 命中穿透本 surface（視為落到其後）。
};

// 單一 surface 對「落在自己身上的事件」的路由決策。
enum class RouteDecision {
    Deliver,     // 遞送本 surface。
    PassBelow,   // 下傳到其下一個 surface。
    Swallow,     // 吞掉：既不遞送本 surface、亦不下傳。
    CaptureAll,  // 全域獨占：所有輸入強制導向捕捉者（無視原命中目標）。
};

// --- 純函式：四態 → 行為 / 對映（無狀態，可獨立測試）---

// 該策略下的命中測試結果。
HitResult hit_result(InputStrategy s) noexcept;

// 該策略對「落在自己身上的事件」的單體路由決策（不考慮堆疊；堆疊解析見 Controller）。
RouteDecision route_decision(InputStrategy s) noexcept;

// 把四態對齊 / 下推到 E1-24 K3 三態 `InputPolicy`。
//   Interactive  -> Accepting   （實心、接受、不獨占）
//   Capture      -> Modal       （獨占焦點）
//   ClickThrough -> PassThrough  （穿透、不吃輸入）
//   Inert        -> Accepting    （實心命中；「吞掉不處理」為 E1-02 路由層細節，
//                                  E1-24 三態無對應，故取命中相符的 Accepting，
//                                  由 Controller 路由層補足吞掉語意）
InputPolicy to_backend_policy(InputStrategy s) noexcept;

// 具名字串（NFR-02：診斷 / 記錄一律具名，不用數字）。未知回 "unknown"。
const char* to_string(InputStrategy s) noexcept;

// 未明確設定時的預設策略（正常互動）。
inline constexpr InputStrategy kDefaultStrategy = InputStrategy::Interactive;

// 啟用 `Capture`（全域獨占捕捉）所需的能力 id（NFR-03 閘控入口）。
// 某些平台限制指標 grab / 全域捕捉，故列為能力；null 預設矩陣未宣告 → 保守不可用。
const CapabilityId& capture_capability_id();

// ---------------------------------------------------------------------------
// InputStrategyController —— 於 `KernelBackend` 之上維護「每 surface 的四態輸入策略」。
//
// 相位 1：全狀態存記憶體，設定時同步下推至 E1-24 K3 `set_input_policy`（對齊三態）。
// 維護一個具名 surface 的堆疊（登記順序即堆疊順序，back = 最上層），供事件路由解析
// 「穿透到其下」「吞掉」「全域獨占」等跨 surface 行為。
// ---------------------------------------------------------------------------
class InputStrategyController {
public:
    // 綁定一個後端（可讀寫其 K3 輸入策略；不擁有其生命週期）。
    explicit InputStrategyController(KernelBackend& backend) noexcept;

    // 設定某具名 surface 的輸入策略。
    //   - surface 須存在於後端（`has_surface`），否則回 false（保守）。
    //   - 設為 `Capture` 需後端具備 capture 能力（`has(capture_capability_id())`，NFR-03）；
    //     不具備則回 false 且不改任何狀態（能力閘控）。
    //   - 成功：記錄策略（首次登記則入堆疊，back=最上層；再次設定則就地更新，保留原位），
    //     並下推至後端 `set_input_policy(id, to_backend_policy(strategy))`。
    bool set_strategy(const SurfaceId& id, InputStrategy strategy);

    // 查詢某具名 surface 目前的輸入策略。未曾設定（或未知 surface）一律回 `kDefaultStrategy`。
    InputStrategy strategy(const SurfaceId& id) const;

    // 該 surface 是否曾被**顯式**設定過策略（用以區分「預設」與「明確設為 Interactive」）。
    bool has_strategy(const SurfaceId& id) const;

    // 從 controller 移除某 surface 的策略記錄（surface 銷毀時呼叫）。回是否確有移除。
    bool forget(const SurfaceId& id);

    // controller 目前追蹤的 surface 數。
    std::size_t tracked_count() const noexcept { return stack_.size(); }

    // 目前是否有某 surface 處於 `Capture`（全域獨占）。有則經 out 回傳其 id。
    bool capture_active(SurfaceId* who = nullptr) const;

    // 單一事件路由的解析結果。
    struct Routed {
        InputEvent event;        // 原事件（具名型別 + 具名目標，無座標）。
        SurfaceId delivered_to;  // 最終遞送的 surface（空 = 無人接收 / 被吞 / 落出堆疊底）。
        RouteDecision decision;  // 決策。
    };

    // 依堆疊與各 surface 策略，把一批事件解析成最終遞送目標。
    //   1. 若有 surface 處於 Capture → 一律 CaptureAll，遞送捕捉者（無視 event.target）。
    //   2. 否則自 event.target 於堆疊之位置向下（其下方）逐層解析：
    //        Interactive → Deliver（停）; ClickThrough → 續往下; Inert → Swallow（停）。
    //      落出堆疊底或 target 不在堆疊 → PassBelow、delivered_to 空。
    std::vector<Routed> route(const std::vector<InputEvent>& events) const;

    // K3 整合：自後端 `poll_input()` 抽一批事件再路由。null 後端永遠回空 → 回空。
    std::vector<Routed> poll_and_route();

private:
    // 具名鍵線性尋找（surface 數量小）。
    std::pair<SurfaceId, InputStrategy>* find(const SurfaceId& id);
    const std::pair<SurfaceId, InputStrategy>* find(const SurfaceId& id) const;
    // 解析單一事件（假設無全域 capture；capture 由 route() 先行處理）。
    Routed resolve_one(const InputEvent& ev) const;

    KernelBackend& backend_;
    // 登記順序 = 堆疊順序（index 0 = 最底、back = 最上層）。永不以數字 index 對外暴露。
    std::vector<std::pair<SurfaceId, InputStrategy>> stack_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_02_INPUT_STRATEGY_HPP
