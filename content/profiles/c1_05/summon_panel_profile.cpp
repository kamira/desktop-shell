// content/profiles/c1_05/summon_panel_profile.cpp — C1-05 召喚面板 profile 實作
#include "summon_panel_profile.hpp"

#include <utility>

namespace ds::profiles {

const char* to_string(PanelState s) noexcept {
    switch (s) {
        case PanelState::Closed:
            return "Closed";
        case PanelState::Open:
            return "Open";
    }
    return "unknown";
}

const char* to_string(CloseReason r) noexcept {
    switch (r) {
        case CloseReason::Manual:
            return "Manual";
        case CloseReason::Timeout:
            return "Timeout";
        case CloseReason::Selected:
            return "Selected";
    }
    return "unknown";
}

const char* to_string(SelectResult r) noexcept {
    switch (r) {
        case SelectResult::Selected:
            return "Selected";
        case SelectResult::NotFound:
            return "NotFound";
        case SelectResult::PanelClosed:
            return "PanelClosed";
    }
    return "unknown";
}

SummonPanelProfile::SummonPanelProfile(std::string id,
                                        ds::kernel::TransientProfileManager& lifecycle,
                                        ds::events::GlobalHotkeys& hotkeys,
                                        ds::kernel::InputStrategy strategy)
    : id_(std::move(id)), lifecycle_(lifecycle), hotkeys_(hotkeys), strategy_(strategy) {
    // 組裝 E1-14：登記本面板的過期回呼進入點。管理器可能被多個短暫 profile 共用，
    // 故 handle_expiry 內部以 id_ 過濾，只處理屬於本面板的過期事件。
    lifecycle_.on_expire([this](const ds::kernel::TransientId& expired_id,
                                 ds::kernel::ExpiryReason reason) {
        handle_expiry(expired_id, reason);
    });
}

SummonPanelProfile::~SummonPanelProfile() {
    // 若仍開啟，強制收起 —— 確保 E1-14 管理器上不再留有指向本已銷毀物件的存活條目
    // （見標頭「已知限制」說明：管理器沒有移除回呼的 API，本物件解構前主動 close()
    // 是唯一能避免懸置回呼被觸發的手段）。
    if (state_ == PanelState::Open) {
        close();
    }
    if (hotkey_id_ != 0) {
        hotkeys_.unregister(hotkey_id_);
        hotkey_id_ = 0;
    }
}

bool SummonPanelProfile::set_items(const ds::format::Value& declarative_list) {
    ds::format::ForestResult result = ds::format::build_forest(declarative_list);
    if (!result.ok()) {
        last_build_error_ = result.error();
        return false;
    }
    items_ = result.items();
    return true;
}

void SummonPanelProfile::set_items(std::vector<ds::format::Item> items) {
    items_ = std::move(items);
}

bool SummonPanelProfile::bind_hotkey(const ds::events::Hotkey& hotkey,
                                     ds::events::Tick summon_ttl) {
    if (hotkey_id_ != 0) {
        return false;  // 已綁定，須先 unbind_hotkey()（不靜默覆寫既有綁定）。
    }
    if (!hotkeys_.has()) {
        return false;  // NFR-03 能力閘控：能力不存在，不呼叫 register_hotkey()。
    }
    ds::events::HotkeyId reg = hotkeys_.register_hotkey(
        hotkey, [this](const ds::events::Hotkey&) {
            // 事件驅動的召喚入口：熱鍵觸發即嘗試叫出面板。若已開啟，open() 回 false
            // 但此處刻意忽略其回傳值 —— 對已開啟面板重複按熱鍵是自然的 no-op，非錯誤。
            this->open(this->summon_ttl_);
        });
    if (reg == 0) {
        return false;  // 無效熱鍵 / 已被佔用 / 能力不存在（底層雙保險）。
    }
    hotkey_id_ = reg;
    summon_ttl_ = summon_ttl;
    return true;
}

bool SummonPanelProfile::unbind_hotkey() {
    if (hotkey_id_ == 0) {
        return false;  // 未曾綁定，no-op。
    }
    bool ok = hotkeys_.unregister(hotkey_id_);
    hotkey_id_ = 0;
    summon_ttl_ = 0;
    return ok;
}

bool SummonPanelProfile::open(ds::events::Tick ttl) {
    if (state_ == PanelState::Open) {
        return false;  // 已開啟中，不靜默重開；呼叫端須先 close()。
    }
    ds::kernel::TransientProfile tp;
    tp.input = strategy_;
    // 面板慣例：顯示於一般視窗之上（Overlay）。lifecycle_.create() 仍會強制
    // lifecycle 為 Ephemeral（短暫語意的單一資料來源，E1-14 契約）。
    tp.surface.layer = ds::kernel::SurfaceLayer::Overlay;

    if (!lifecycle_.create(id_, tp, ttl)) {
        return false;  // 委派 E1-14：ttl == 0、id 已存在存活等一律回 false。
    }
    state_ = PanelState::Open;
    for (auto& cb : on_open_) {
        cb();
    }
    return true;
}

bool SummonPanelProfile::close() {
    if (state_ != PanelState::Open) {
        return false;  // 未開啟，no-op，不靜默。
    }
    pending_manual_reason_ = CloseReason::Manual;
    return lifecycle_.expire(id_);  // 觸發 handle_expiry -> 更新狀態 + on_close(Manual)。
}

std::vector<const ds::format::Item*> SummonPanelProfile::filter(const std::string& query) const {
    std::vector<const ds::format::Item*> out;
    if (state_ != PanelState::Open) {
        return out;  // 面板未開啟，不得操作。
    }
    for (const auto& root : items_) {
        ds::format::for_each_preorder(root, [&](const ds::format::Item& node, int /*depth*/) {
            if (query.empty() || node.id().find(query) != std::string::npos ||
                node.label().find(query) != std::string::npos) {
                out.push_back(&node);
            }
        });
    }
    return out;
}

SelectResult SummonPanelProfile::select(const std::string& item_id,
                                        const ds::format::Item** out) {
    if (state_ != PanelState::Open) {
        return SelectResult::PanelClosed;
    }
    const ds::format::Item* found = nullptr;
    for (const auto& root : items_) {
        found = root.find(item_id);
        if (found != nullptr) {
            break;
        }
    }
    if (found == nullptr) {
        return SelectResult::NotFound;  // 面板保持開啟。
    }
    if (out != nullptr) {
        *out = found;
    }
    for (auto& cb : on_select_) {
        cb(*found);
    }
    // 標記本次收起原因為 Selected（非一般手動 close），再交由 E1-14 expire() 觸發
    // handle_expiry（其內以此旗標決定 CloseReason，再重置回 Manual 供下次一般手動關閉用）。
    pending_manual_reason_ = CloseReason::Selected;
    lifecycle_.expire(id_);
    pending_manual_reason_ = CloseReason::Manual;
    return SelectResult::Selected;
}

ds::kernel::InputPolicy SummonPanelProfile::backend_input_policy() const noexcept {
    return ds::kernel::to_backend_policy(strategy_);
}

ds::kernel::InputHitResult SummonPanelProfile::hit_result() const noexcept {
    return ds::kernel::hit_result(strategy_);
}

void SummonPanelProfile::on_open(std::function<void()> cb) {
    on_open_.push_back(std::move(cb));
}

void SummonPanelProfile::on_close(std::function<void(CloseReason)> cb) {
    on_close_.push_back(std::move(cb));
}

void SummonPanelProfile::on_select(std::function<void(const ds::format::Item&)> cb) {
    on_select_.push_back(std::move(cb));
}

void SummonPanelProfile::handle_expiry(const ds::kernel::TransientId& expired_id,
                                       ds::kernel::ExpiryReason reason) {
    if (expired_id != id_) {
        return;  // 非本面板的過期事件（管理器可能共用），忽略。
    }
    state_ = PanelState::Closed;
    CloseReason cr = (reason == ds::kernel::ExpiryReason::Timeout) ? CloseReason::Timeout
                                                                    : pending_manual_reason_;
    for (auto& cb : on_close_) {
        cb(cr);
    }
}

}  // namespace ds::profiles
