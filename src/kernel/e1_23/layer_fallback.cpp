// E1-23 分層失敗的手動降級路徑 — 實作
//
// 純記憶體邏輯，無平台分支、無真實 OS API。降級決策全由 FallbackStrategy（預設即
// has(kernel.surface)）與 LayerStack::assign() 的結果碼驅動；降級發生時一律透過
// LayerFallbackResult / degrade_reason() 可見回報，不靜默吞掉。
#include "layer_fallback.hpp"

#include <utility>

namespace ds::kernel {

LayerFallback::LayerFallback(std::shared_ptr<FallbackStrategy> strategy)
    : strategy_(std::move(strategy)) {
    if (!strategy_) {
        // 保守：注入 nullptr 時退回內建預設策略，不留下可能的空指標風險。
        strategy_ = std::make_shared<FallbackStrategy>();
    }
}

void LayerFallback::set_strategy(std::shared_ptr<FallbackStrategy> strategy) {
    if (strategy) {
        strategy_ = std::move(strategy);
    }
    // nullptr：保守忽略，維持目前策略不變（不留下空指標風險）。
}

LayerFallbackResult LayerFallback::do_fallback(LayerStack& stack, const SurfaceId& id,
                                                 FallbackReason reason) {
    const SurfaceLayer target = strategy_->flatten_target();
    const LayerAssign result = stack.assign(id, target);
    last_reason_ = reason;
    last_degraded_ = true;
    return LayerFallbackResult{/*degraded=*/true, reason, result, target};
}

LayerFallbackResult LayerFallback::try_layered(LayerStack& stack, const SurfaceId& id,
                                                 SurfaceLayer requested_layer) {
    // NFR-03：降級決策由 has() 能力查詢驅動（經策略，預設即 stack.has(layer_capability())）。
    if (!strategy_->layered_available(stack)) {
        return do_fallback(stack, id, FallbackReason::CapabilityUnavailable);
    }
    const LayerAssign result = stack.assign(id, requested_layer);
    if (result == LayerAssign::Ok || result == LayerAssign::Moved) {
        // 正常分層路徑：不降級。
        last_reason_ = FallbackReason::None;
        last_degraded_ = false;
        return LayerFallbackResult{/*degraded=*/false, FallbackReason::None, result,
                                    requested_layer};
    }
    // assign 失敗（如空 id）：非能力問題，仍手動降級（可見回報，不靜默）。
    return do_fallback(stack, id, FallbackReason::AssignRejected);
}

LayerFallbackResult LayerFallback::fallback_flatten(LayerStack& stack, const SurfaceId& id) {
    // 手動降級：即使分層能力目前可用，呼叫端仍主動要求降級 → 原因為 StrategyForced；
    // 能力本就不可用時，仍如實回報真正原因 CapabilityUnavailable（不覆蓋事實）。
    const FallbackReason reason = strategy_->layered_available(stack)
                                       ? FallbackReason::StrategyForced
                                       : FallbackReason::CapabilityUnavailable;
    return do_fallback(stack, id, reason);
}

}  // namespace ds::kernel
