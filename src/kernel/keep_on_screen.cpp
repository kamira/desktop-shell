// E1-10 保持在螢幕內 — 實作（純位置夾回計算）
//
// 相位 1：無真實視窗 / 螢幕查詢 API、無平台分支（無 #ifdef / win32 / cocoa）。所有數學為
// 平台中立的夾回計算。無效輸入結構化回報，不靜默、不崩潰。
#include "keep_on_screen.hpp"

#include <algorithm>  // std::max, std::min
#include <cmath>      // std::isfinite

namespace ds::kernel {

namespace {

// placement 的 width / height 是否為有限且非負（x / y 允許任意有限值，含負值 ——
// 那正是「已超出左 / 上邊界」的合法輸入態樣）。
bool is_valid_placement(const ResolvedPlacement& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.width) &&
           std::isfinite(p.height) && p.width >= 0.0f && p.height >= 0.0f;
}

// 單軸夾回：把 [pos, pos+extent] 夾回 [0, bound]。
// 若 extent > bound（元件大於螢幕該軸），可容納範圍退化為 0 —— 貼齊起邊（0），
// 不縮放、不裁切元件本身，只讓其盡量呈現於可視範圍起點。
float clamp_axis(float pos, float extent, float bound) {
    const float max_pos = std::max(0.0f, bound - extent);
    return std::min(std::max(pos, 0.0f), max_pos);
}

}  // namespace

bool is_within_screen(const ResolvedPlacement& placement, const Size& screen) {
    if (!is_valid_placement(placement) || !is_finite_size(screen)) {
        return false;  // 保守
    }
    const bool within_x = placement.x >= 0.0f && placement.x + placement.width <= screen.width;
    const bool within_y = placement.y >= 0.0f && placement.y + placement.height <= screen.height;
    return within_x && within_y;
}

KeepOnScreenStatus constrain(const ResolvedPlacement& placement, const Size& screen,
                             ResolvedPlacement& out) {
    if (!is_valid_placement(placement) || !is_finite_size(screen)) {
        return KeepOnScreenStatus::Invalid;  // 不觸碰 out（不靜默）
    }

    ResolvedPlacement adjusted;
    adjusted.width = placement.width;
    adjusted.height = placement.height;
    adjusted.x = clamp_axis(placement.x, placement.width, screen.width);
    adjusted.y = clamp_axis(placement.y, placement.height, screen.height);

    out = adjusted;
    return KeepOnScreenStatus::Ok;
}

KeepOnScreenStatus KeepOnScreen::constrain_on(const ScreenId& screen_id, const Size& screen_size,
                                               const ResolvedPlacement& placement,
                                               ResolvedPlacement& out) const {
    if (!registry_->is_known(screen_id)) {
        return KeepOnScreenStatus::Invalid;  // 不猜測未知具名螢幕的尺寸
    }
    return constrain(placement, screen_size, out);
}

}  // namespace ds::kernel
