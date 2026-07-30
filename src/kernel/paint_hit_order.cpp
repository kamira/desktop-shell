// E1-06 命中與繪製層序 — 實作
//
// 純序列邏輯：依繪製反序逐一呼叫上游 E1-04 `HitTester::hit_test_alpha`，不重新實作任何
// 幾何 / alpha 判定；不含任何平台分支、真實 OS / 繪圖 API。
#include "paint_hit_order.hpp"

#include <utility>

namespace ds::kernel {

void PaintHitOrder::set_paint_order(std::vector<HitSurface> surfaces) {
    surfaces_ = std::move(surfaces);
}

TopmostHit PaintHitOrder::hit_topmost(const LocalPoint& point) const {
    TopmostHit result{HitStatus::Ok, false, SurfaceId{}};
    // 依繪製反序走訪：從最後繪製（視覺最上層）走到最先繪製（視覺最底層）。
    for (std::size_t idx = surfaces_.size(); idx > 0; --idx) {
        const HitSurface& s = surfaces_[idx - 1];
        const HitResult r = tester_.hit_test_alpha(point, s);
        if (r.status != HitStatus::Ok) {
            return TopmostHit{HitStatus::Invalid, false, SurfaceId{}};  // 報錯不靜默
        }
        // 反序走訪中第一個命中者即視覺最上層命中者；之後（更底層）的命中不覆蓋既有結果，
        // 但仍需完成走訪以偵測較底層 surface 是否無效（語意同 E1-04 topmost_hit）。
        if (r.inside && !result.hit) {
            result.hit = true;
            result.id = s.id;
        }
    }
    return result;
}

}  // namespace ds::kernel
