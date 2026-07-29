// content/profiles/c1_04/osd_overlay_profile.cpp — C1-04 OSD 浮層 profile 實作
#include "osd_overlay_profile.hpp"

#include <utility>

namespace ds::profiles {

const char* to_string(OsdState s) noexcept {
    switch (s) {
        case OsdState::Hidden:
            return "Hidden";
        case OsdState::Showing:
            return "Showing";
    }
    return "unknown";
}

const char* to_string(DismissReason r) noexcept {
    switch (r) {
        case DismissReason::Manual:
            return "Manual";
        case DismissReason::Timeout:
            return "Timeout";
    }
    return "unknown";
}

OsdOverlayProfile::OsdOverlayProfile(std::string id,
                                     ds::kernel::TransientProfileManager& lifecycle,
                                     ds::kernel::LayerStack& layers,
                                     ds::kernel::InputStrategy strategy)
    : id_(std::move(id)), lifecycle_(lifecycle), layers_(layers), strategy_(strategy) {
    // 組裝 E1-14：登記本 OSD 的過期回呼進入點。管理器可能被多個短暫 profile（含其他
    // OSD）共用，故 handle_expiry 內部以 id_ 過濾，只處理屬於本 OSD 的過期事件。
    lifecycle_.on_expire([this](const ds::kernel::TransientId& expired_id,
                                 ds::kernel::ExpiryReason reason) {
        handle_expiry(expired_id, reason);
    });
}

OsdOverlayProfile::~OsdOverlayProfile() {
    // 若仍顯示中，強制收起 —— 確保 E1-14 管理器 / E1-01 圖層堆疊上不再留有指向本已銷毀
    // 物件的存活條目（見標頭「已知限制」說明）。
    if (state_ == OsdState::Showing) {
        dismiss();
    }
}

bool OsdOverlayProfile::show(std::string message, ds::events::Tick ttl) {
    if (state_ == OsdState::Showing) {
        return false;  // 顯示中，不靜默重顯；呼叫端須先 dismiss() 或改用 update()。
    }

    ds::kernel::TransientProfile tp;
    tp.input = strategy_;
    // OSD 慣例：顯示於一般視窗之上（Overlay）。lifecycle_.create() 仍會強制 lifecycle
    // 為 Ephemeral（短暫語意的單一資料來源，E1-14 契約）。
    tp.surface.layer = ds::kernel::SurfaceLayer::Overlay;

    if (!lifecycle_.create(id_, tp, ttl)) {
        return false;  // 委派 E1-14：ttl == 0、id 已存在存活等一律回 false。不動 layers。
    }

    // 組裝 E1-01：把本 OSD 指派到具名頂層。失敗（空 id / 能力不可用）→ 回滾剛建立的
    // E1-14 短暫 profile 條目，不留下「lifecycle 有記錄但未歸屬任何圖層」的不一致狀態。
    ds::kernel::LayerAssign assigned = layers_.assign(id_, ds::kernel::SurfaceLayer::Overlay);
    if (assigned == ds::kernel::LayerAssign::RejectedEmptyId ||
        assigned == ds::kernel::LayerAssign::RejectedNoCapability) {
        lifecycle_.expire(id_);  // 回滾；handle_expiry 會因狀態守衛（見下）而不誤觸發
                                  // 使用者可感知的「收起」語意，因為此時尚未進入 Showing。
        return false;
    }

    message_ = std::move(message);
    state_ = OsdState::Showing;
    for (auto& cb : on_show_) {
        cb(message_);
    }
    return true;
}

bool OsdOverlayProfile::update(std::string message) {
    if (state_ != OsdState::Showing) {
        return false;  // 未顯示，no-op，不靜默。
    }
    message_ = std::move(message);
    for (auto& cb : on_update_) {
        cb(message_);
    }
    return true;
}

bool OsdOverlayProfile::dismiss() {
    if (state_ != OsdState::Showing) {
        return false;  // 未顯示，no-op，不靜默。
    }
    return lifecycle_.expire(id_);  // 觸發 handle_expiry -> 更新狀態 + 移除頂層 + on_dismiss(Manual)。
}

std::string OsdOverlayProfile::layer_name() const {
    return ds::kernel::layer_name(layer());
}

bool OsdOverlayProfile::assigned_to_layer_stack() const {
    return layers_.contains(id_);
}

ds::kernel::InputPolicy OsdOverlayProfile::backend_input_policy() const noexcept {
    return ds::kernel::to_backend_policy(strategy_);
}

ds::kernel::HitResult OsdOverlayProfile::hit_result() const noexcept {
    return ds::kernel::hit_result(strategy_);
}

void OsdOverlayProfile::on_show(std::function<void(const std::string&)> cb) {
    on_show_.push_back(std::move(cb));
}

void OsdOverlayProfile::on_update(std::function<void(const std::string&)> cb) {
    on_update_.push_back(std::move(cb));
}

void OsdOverlayProfile::on_dismiss(std::function<void(DismissReason)> cb) {
    on_dismiss_.push_back(std::move(cb));
}

void OsdOverlayProfile::handle_expiry(const ds::kernel::TransientId& expired_id,
                                       ds::kernel::ExpiryReason reason) {
    if (expired_id != id_) {
        return;  // 非本 OSD 的過期事件（管理器可能共用），忽略。
    }
    if (state_ != OsdState::Showing) {
        // show() 內 layers_.assign() 失敗回滾時會觸發本回呼，但此時本物件尚未進入
        // Showing（回滾發生在狀態切換之前）——不是「使用者可感知的收起」，靜默忽略，
        // 不觸發 on_dismiss、也不誤動 layers（layers.assign 本就未成功，無需 remove）。
        return;
    }
    state_ = OsdState::Hidden;
    layers_.remove(id_);  // E1-01：連動移除具名頂層歸屬（忽略回傳值——即使能力此刻不可用
                           // 而移除失敗，OSD 本身的顯示狀態仍必須收起，不因此卡住）。
    DismissReason dr =
        (reason == ds::kernel::ExpiryReason::Timeout) ? DismissReason::Timeout : DismissReason::Manual;
    for (auto& cb : on_dismiss_) {
        cb(dr);
    }
}

}  // namespace ds::profiles
