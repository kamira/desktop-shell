// E1-05 具名碰撞區域 → 事件參數 — 實作
//
// 純幾何邏輯，建於上游 E1-04 `HitTester` 之上；不含任何平台分支、真實 OS / 繪圖 API。
#include "named_region_map.hpp"

#include <algorithm>  // std::find_if, std::any_of
#include <cmath>      // std::isfinite

namespace ds::kernel {

namespace {

bool finite_point(const LocalPoint& p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}

}  // namespace

bool NamedRegionMap::add_region(const std::string& name, Shape shape, RegionParams params) {
    if (name.empty()) {
        return false;  // 具名指涉不得為空（報錯不靜默）
    }
    if (!tester_.is_valid(shape)) {
        return false;  // 無效形狀：拒絕新增（報錯不靜默，NFR-02 精神延伸）
    }
    if (has_region(name)) {
        return false;  // 重複具名：拒絕新增（報錯不靜默，避免同名區域語意混淆）
    }
    regions_.emplace_back(name, RegionRecord{std::move(shape), std::move(params)});
    return true;
}

bool NamedRegionMap::remove_region(const std::string& name) {
    const auto it = std::find_if(regions_.begin(), regions_.end(),
                                  [&name](const auto& kv) { return kv.first == name; });
    if (it == regions_.end()) {
        return false;  // 未知具名：不崩潰
    }
    regions_.erase(it);
    return true;
}

bool NamedRegionMap::has_region(const std::string& name) const {
    return std::any_of(regions_.begin(), regions_.end(),
                        [&name](const auto& kv) { return kv.first == name; });
}

std::size_t NamedRegionMap::region_count() const { return regions_.size(); }

RegionHit NamedRegionMap::hit(const LocalPoint& point) const {
    if (!finite_point(point)) {
        return RegionHit{HitStatus::Invalid, false, std::string{}, RegionParams{}};
    }
    RegionHit result{HitStatus::Ok, false, std::string{}, RegionParams{}};
    for (const auto& entry : regions_) {
        const HitResult r = tester_.hit_test(point, entry.second.shape);
        if (r.status != HitStatus::Ok) {
            // 形狀已於 add_region 時驗證過，理論上不會落入此分支；防禦性報錯不靜默。
            return RegionHit{HitStatus::Invalid, false, std::string{}, RegionParams{}};
        }
        if (r.inside) {
            // 加入序後者為上：持續覆寫，迴圈結束時留下最後一個命中者（NFR-02：無數字 z-order）。
            result.hit = true;
            result.name = entry.first;
            result.params = entry.second.params;
        }
    }
    return result;
}

}  // namespace ds::kernel
