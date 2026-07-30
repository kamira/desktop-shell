// E1-17 DPI 感知與混合 DPI — 實作
//
// 內嵌預設拓撲 + 平台中立的查詢邏輯。此檔不含任何平台分支、真實後端，或絕對座標。
#include "dpi_awareness.hpp"

#include <utility>

namespace ds::kernel {

namespace {

// 內嵌的預設 DPI 拓撲 —— 相位 1（null 期）的單一資料來源。
//
// null 後端尚無真實 per-monitor DPI 探測：只宣告一個具名主螢幕，縮放為中性的
// kDefaultScaleFactor。真實後端上線後由後端以實際多螢幕探測覆寫（可含混合 DPI）。
// 純資料、不含平台判斷 —— 換平台時**不動這裡**，只換提供實際探測的後端。
const std::vector<ScreenDpi>& default_screens() {
    static const std::vector<ScreenDpi> kScreens = {
        {"screen.primary", "主螢幕（null 期預設，縮放待真實後端探測）", kDefaultScaleFactor},
    };
    return kScreens;
}

// 正規化縮放係數：非正值退回中性預設（保守）。
double normalize_scale(double s) {
    return (s > 0.0) ? s : kDefaultScaleFactor;
}

}  // namespace

DpiInfo::DpiInfo(std::vector<ScreenDpi> screens) : screens_(std::move(screens)) {
    for (auto& s : screens_) {
        s.scale_factor = normalize_scale(s.scale_factor);
    }
}

DpiInfo DpiInfo::defaults() {
    return DpiInfo(default_screens());
}

const ScreenDpi* DpiInfo::find(const ScreenId& id) const {
    // 後定義者為準：反向掃描，讓重複 id 時最後一筆宣告勝出。
    for (auto it = screens_.rbegin(); it != screens_.rend(); ++it) {
        if (it->id == id) {
            return &(*it);
        }
    }
    return nullptr;
}

bool DpiInfo::is_known(const ScreenId& id) const {
    return find(id) != nullptr;
}

double DpiInfo::scale_of(const ScreenId& id) const {
    const ScreenDpi* s = find(id);
    // 保守：未知螢幕回中性預設，呼叫端永不誤縮放。
    return s != nullptr ? s->scale_factor : kDefaultScaleFactor;
}

double DpiInfo::relative_scale(const ScreenId& from, const ScreenId& to) const {
    // 純比值，無絕對座標。scale_of 已保證分母 >= 正的預設，不會除以零。
    return scale_of(to) / scale_of(from);
}

bool DpiInfo::is_mixed_dpi() const {
    // 存在至少兩個縮放係數不同的螢幕即為混合 DPI。各螢幕彼此獨立比較。
    for (std::size_t i = 0; i < screens_.size(); ++i) {
        for (std::size_t j = i + 1; j < screens_.size(); ++j) {
            if (screens_[i].scale_factor != screens_[j].scale_factor) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace ds::kernel
