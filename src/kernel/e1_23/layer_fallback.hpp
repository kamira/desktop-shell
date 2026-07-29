// E1-23 分層失敗的手動降級路徑 — 宣告檔（platform 層 / 子系統 kernel / 相位 1 null）
//
// 部分平台 / 情境下，具名圖層合成能力不被支援（`has(kernel.surface) == false`）或
// 指派操作本身失敗（`LayerStack::assign()` 回非 Ok/Moved）。本單元建於上游 E1-01
// （具名圖層 `LayerStack`）與 E1-21（能力矩陣 `has()`）之上，提供一條**明確的手動
// 降級路徑**：
//   1. `try_layered(...)` —— 先嘗試正常的分層路徑；能力不可用或 assign 失敗時，
//      自動改走降級扁平化（不靜默失敗、不崩潰）。
//   2. `fallback_flatten(...)` —— 直接手動降級：忽略請求圖層，扁平化到策略決定的
//      單一具名圖層（預設 `SurfaceLayer::Normal`，無 overlay）。
//   3. `degrade_reason()` / 結果中的 `reason` —— 降級是否發生、發生的原因**可見回報**，
//      不靜默吞掉。
//   4. `FallbackStrategy` —— 可注入的降級策略：決定「該不該降級」與「降級後扁平化到
//      哪個具名圖層」，供呼叫端客製降級行為（如測試強制降級、選不同扁平目的層）。
//
// NFR-03 精神：降級決策由 `has()` 能力查詢驅動（透過 `FallbackStrategy::layered_available`，
//   預設即 `stack.has(layer_capability())`）；assign 失敗（如空 id）亦觸發降級，同樣可見回報。
// NFR-02：全程只用具名圖層 `SurfaceLayer` / 具名 `SurfaceId`，無數字 z-order / 絕對座標。
//
// 相位 1（Mac / null 期）：純介面 + 記憶體邏輯，無 `#ifdef` / win32 / cocoa 平台分支，
//   不觸碰任何真實 OS 視窗 / 合成器 API。
#ifndef DS_KERNEL_E1_23_LAYER_FALLBACK_HPP
#define DS_KERNEL_E1_23_LAYER_FALLBACK_HPP

#include <memory>

#include "layer_stack.hpp"  // 上游 E1-01：LayerStack / LayerAssign / layer_capability()（可讀不可改）

namespace ds::kernel {

// 降級原因 —— 這次操作「有沒有」降級、降級的原因為何（可見不靜默）。
enum class FallbackReason {
    None,                    // 未降級：分層路徑正常完成
    CapabilityUnavailable,   // 分層能力不可用（策略判定 has(kernel.surface) == false）
    AssignRejected,          // 分層 assign() 失敗（如空 id），非能力問題
    StrategyForced,          // 呼叫端手動要求降級（即使分層能力目前可用）
};

// 降級策略 —— 可注入：決定「分層能力是否視為可用」與「降級後扁平化到哪個具名圖層」。
// 預設策略：以 has(kernel.surface) 判斷可用性（NFR-03）；降級目的層固定為 Normal（無 overlay）。
class FallbackStrategy {
public:
    virtual ~FallbackStrategy() = default;

    // 分層能力是否視為可用。預設委由 has() 能力閘控（NFR-03）；子類別可覆寫以注入
    // 自訂降級判斷（例如測試中強制視為不可用，或加上額外情境條件）。
    virtual bool layered_available(const LayerStack& stack) const {
        return stack.has(layer_capability());
    }

    // 降級後扁平化的目的具名圖層（NFR-02：具名，不是數字）。預設 Normal：
    // 退回最單純的單層呈現，不使用 Overlay / Topmost 等疊加語意。
    virtual SurfaceLayer flatten_target() const { return SurfaceLayer::Normal; }
};

// 一次「嘗試分層 / 手動降級」操作的結果 —— 讓呼叫端可分辨走了哪條路徑、有無降級、
// 降級原因、實際指派結果與生效圖層（可見不靜默，NFR-03 精神）。
struct LayerFallbackResult {
    bool degraded = false;                                      // 這次操作是否走了降級路徑
    FallbackReason reason = FallbackReason::None;                // 降級原因（None = 未降級）
    LayerAssign assign_result = LayerAssign::RejectedEmptyId;    // 實際 LayerStack::assign() 結果碼
    SurfaceLayer effective_layer = SurfaceLayer::Normal;         // 實際嘗試指派到的具名圖層
};

// ---------------------------------------------------------------------------
// LayerFallback —— 分層失敗時的手動降級路徑器（相位 1 記憶體邏輯）。
//
// 不持有 LayerStack：每次操作以參數傳入，讓同一個 LayerFallback 可用於多個 LayerStack
// 實例（例如「能力不可用的 stack」與「能力恢復後的 stack」），貼近降級決策應隨當下
// 能力狀態而定、而非綁死在建構當下的語意。
// ---------------------------------------------------------------------------
class LayerFallback {
public:
    // 預設以內建 FallbackStrategy 建構（單層扁平化到 Normal）；亦可注入自訂策略。
    // 注入 nullptr 時保守退回內建預設策略。
    explicit LayerFallback(std::shared_ptr<FallbackStrategy> strategy =
                                std::make_shared<FallbackStrategy>());

    // --- 降級策略（可注入 / 可查詢）---
    // 注入 nullptr 時忽略（維持目前策略），不留下可能的空指標風險。
    void set_strategy(std::shared_ptr<FallbackStrategy> strategy);
    const FallbackStrategy& strategy() const { return *strategy_; }

    // --- 主要入口：嘗試分層路徑，必要時自動手動降級 ---
    // 1) 策略判定分層能力不可用 → 直接降級扁平化（原因 CapabilityUnavailable）。
    // 2) 分層能力可用但 stack.assign(id, requested_layer) 失敗（如空 id）
    //    → 降級扁平化（原因 AssignRejected）。
    // 3) 分層能力可用且 assign 成功（Ok / Moved）→ 正常路徑，不降級。
    // 全程不崩潰；降級與否、原因皆記錄於回傳結果與 degrade_reason() / last_degraded()。
    LayerFallbackResult try_layered(LayerStack& stack, const SurfaceId& id,
                                     SurfaceLayer requested_layer);

    // --- 手動降級路徑：忽略請求圖層，直接扁平化到策略決定的具名圖層 ---
    // 供呼叫端主動選擇降級（不透過 try_layered 的自動判定），例如已知情境需要降級、
    // 或供測試直接驗證降級行為。降級原因依當下能力狀態判定：
    //   能力不可用 → CapabilityUnavailable；能力可用但仍手動要求 → StrategyForced。
    LayerFallbackResult fallback_flatten(LayerStack& stack, const SurfaceId& id);

    // --- 最近一次操作的降級可見回報（NFR-03 精神：可見不靜默）---
    FallbackReason degrade_reason() const noexcept { return last_reason_; }
    bool last_degraded() const noexcept { return last_degraded_; }

private:
    LayerFallbackResult do_fallback(LayerStack& stack, const SurfaceId& id,
                                     FallbackReason reason);

    std::shared_ptr<FallbackStrategy> strategy_;
    FallbackReason last_reason_ = FallbackReason::None;
    bool last_degraded_ = false;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_23_LAYER_FALLBACK_HPP
