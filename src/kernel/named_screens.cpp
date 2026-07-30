// E1-18 具名螢幕與每螢幕實例 — 實作
//
// 內嵌預設拓撲 + 平台中立的列舉 / 查詢邏輯。此檔不含任何平台分支、真實後端，或絕對座標。
#include "named_screens.hpp"

#include <utility>

namespace ds::kernel {

namespace {

// 內嵌的預設螢幕拓撲 —— 相位 1（null 期）的單一資料來源。
//
// null 後端尚無真實多螢幕列舉：只宣告一個具名主螢幕，角色 Primary、錨點 Center。
// 真實後端上線後由後端以實際列舉覆寫（可含多螢幕與各自的具名錨點）。
// 純資料、不含平台判斷 —— 換平台時**不動這裡**，只換提供實際列舉的後端。
const std::vector<Screen>& default_screens() {
    static const std::vector<Screen> kScreens = {
        {"screen.primary", "主螢幕（null 期預設，拓撲待真實後端列舉）",
         ScreenRole::Primary, ScreenAnchor::Center},
    };
    return kScreens;
}

}  // namespace

ScreenRegistry::ScreenRegistry(std::vector<Screen> screens)
    : screens_(std::move(screens)) {}

ScreenRegistry ScreenRegistry::defaults() {
    return ScreenRegistry(default_screens());
}

const Screen* ScreenRegistry::find(const ScreenId& id) const {
    // 後定義者為準：反向掃描，讓重複 id 時最後一筆宣告勝出（供後端覆寫先前宣告）。
    for (auto it = screens_.rbegin(); it != screens_.rend(); ++it) {
        if (it->id == id) {
            return &(*it);
        }
    }
    return nullptr;
}

bool ScreenRegistry::is_known(const ScreenId& id) const {
    return find(id) != nullptr;
}

std::vector<ScreenId> ScreenRegistry::ids() const {
    // 對外的列舉入口：一律以具名 ScreenId，永不暴露數字 index。
    std::vector<ScreenId> out;
    out.reserve(screens_.size());
    for (const auto& s : screens_) {
        out.push_back(s.id);
    }
    return out;
}

const Screen* ScreenRegistry::primary() const {
    // 以具名角色決定主螢幕，不用數字層級：第一個 Primary 勝出。
    for (const auto& s : screens_) {
        if (s.role == ScreenRole::Primary) {
            return &s;
        }
    }
    // 保守退回：無明確 Primary 時取第一個螢幕；空拓撲回 nullptr。
    return screens_.empty() ? nullptr : &screens_.front();
}

bool ScreenRegistry::is_primary(const ScreenId& id) const {
    const Screen* s = find(id);
    // 保守：未知螢幕非主螢幕。
    return s != nullptr && s->role == ScreenRole::Primary;
}

ScreenAnchor ScreenRegistry::anchor_of(const ScreenId& id) const {
    const Screen* s = find(id);
    // 保守：未知螢幕回中性參考錨 Center，呼叫端永遠安全。
    return s != nullptr ? s->anchor : ScreenAnchor::Center;
}

}  // namespace ds::kernel
