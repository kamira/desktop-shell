// content/profiles/c1_03/balloon_profile.cpp — C1-03 氣球 profile 實作（組裝型 artifact 單元）
//
// E1-14 `TransientProfileManager` 的實際串接以 pimpl（`Impl`）隔離於此檔——理由見
// balloon_profile.hpp 開頭說明的既有上游命名碰撞：`transient_profile.hpp` 經
// `#include "input_strategy.hpp"`（E1-02，宣告 `enum class ds::kernel::HitResult`）與 C1-02
// （經 character_bridge.cpp 隔離，transitively 帶入 E1-04 `hit_test.hpp` 之
// `struct ds::kernel::HitResult`）若同時出現在同一翻譯單元會編譯失敗（本機以 g++ 實測重現，
// 與 content/profiles/c1_06 已記錄之相同手法）。本檔因此是本單元**唯一**
// `#include "transient_profile.hpp"` 之處，且不 `#include "portrait_profile.hpp"`（角色狀態
// 改經 `character_bridge.hpp` 的中立函式讀取，其 `.cpp` 才真正引入 C1-02）。
//
// 相位 1：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 #ifdef / win32 / cocoa）、無絕對座標 /
// 數字 z-order（NFR-02）。無效輸入結構化回報，不靜默；逾時 / 手動消失共用同一條收尾路徑。
#include "balloon_profile.hpp"

#include <utility>  // std::move

#include "transient_profile.hpp"  // E1-14（上游，可讀不可改）：TransientProfileManager /
                                   //   TransientProfile / TransientId / ExpiryReason；經其標頭
                                   //   傳遞 E5-10 timeout_timer.hpp（TimeoutTimer）與 E1-02
                                   //   input_strategy.hpp（InputStrategy）

namespace ds::profiles {

const char* to_string(BalloonState s) noexcept {
    switch (s) {
        case BalloonState::Hidden:
            return "Hidden";
        case BalloonState::Showing:
            return "Showing";
    }
    return "unknown";
}

const char* to_string(BalloonStatus s) noexcept {
    switch (s) {
        case BalloonStatus::Ok:
            return "Ok";
        case BalloonStatus::Invalid:
            return "Invalid";
        case BalloonStatus::AlreadyShowing:
            return "AlreadyShowing";
    }
    return "unknown";
}

// 本氣球專屬的 E1-14 服務：獨立的 E5-10 計時器 + 綁定其上的管理器（不與其他 BalloonProfile
// 共用，天然支援多氣球互不干擾）。`Impl` 為 `BalloonProfile` 的巢狀類別，依標準規則可存取其
// private 成員（`id_` / `teardown_display()`）。
struct BalloonProfile::Impl {
    explicit Impl(BalloonProfile& owner) : transient(timer) {
        // 逾時（Timeout）與手動（Manual）到期共用同一條收尾路徑；只處理本氣球自身的 id（本
        // 管理器專屬本氣球，理論上不會收到其他 id，此檢查為防禦性一致性保障）。
        transient.on_expire(
            [&owner](const ds::kernel::TransientId& expired_id, ds::kernel::ExpiryReason) {
                if (expired_id == owner.id_) {
                    owner.teardown_display();
                }
            });
    }

    ds::events::TimeoutTimer timer;
    ds::kernel::TransientProfileManager transient;
};

BalloonProfile::BalloonProfile(std::string id, const ds::render::FontMetrics& metrics,
                               ds::render::LayoutConstraints constraints)
    : id_(std::move(id)),
      typewriter_(metrics, constraints, id_),  // 目標 surface 即本氣球自身（NFR-02 具名指涉）
      impl_(std::make_unique<Impl>(*this)) {}

BalloonProfile::~BalloonProfile() = default;

void BalloonProfile::teardown_display() {
    layout_.detach(id_);  // 未附著時安全 no-op（回 Invalid，忽略）
    typewriter_.reset();
    parent_id_.clear();
    state_ = BalloonState::Hidden;
}

BalloonStatus BalloonProfile::show_balloon(const PortraitProfile& character, const std::string& text,
                                           ds::events::Tick ttl, const ds::kernel::AnchorSpec& spec) {
    if (id_.empty() || text.empty()) {
        return BalloonStatus::Invalid;  // 空文字：不靜默顯示空氣球
    }
    if (state_ == BalloonState::Showing) {
        return BalloonStatus::AlreadyShowing;  // 不靜默覆寫；先 dismiss() 或等逾時
    }

    std::string character_id;
    if (!detail::character_snapshot(character, character_id)) {
        return BalloonStatus::Invalid;  // 未載入的角色無有效 surface 可依附
    }
    if (ttl == 0) {
        return BalloonStatus::Invalid;
    }

    // E4-11：先設定顯示文字（進度隨之歸零）。尚未觸碰 layout_ / impl_，若此處因非法 UTF-8
    // 擲例外，本物件狀態不變（例外安全）。
    typewriter_.set_text(text);

    // E1-11：依附角色（具名 anchor + 相對偏移）。無效 spec / 自附（角色與本氣球同名）等 → 回滾。
    if (layout_.attach(id_, character_id, spec) != ds::kernel::AnchorStatus::Ok) {
        typewriter_.reset();
        return BalloonStatus::Invalid;
    }

    // E1-14：登記 ttl 個 tick 後自動消失。理論上不會失敗（前置條件皆已驗證），仍防禦性回滾。
    ds::kernel::TransientProfile profile;
    profile.input = ds::kernel::InputStrategy::ClickThrough;  // 對話氣球屬提示語意，不吃輸入
    if (!impl_->transient.create(id_, profile, ttl)) {
        layout_.detach(id_);
        typewriter_.reset();
        return BalloonStatus::Invalid;
    }

    parent_id_ = character_id;
    state_ = BalloonState::Showing;
    return BalloonStatus::Ok;
}

void BalloonProfile::advance(ds::events::Tick dt) {
    if (state_ != BalloonState::Showing) {
        return;  // 未顯示中：no-op
    }
    impl_->transient.advance(dt);  // 可能於本次推進中觸發逾時消失（同步呼叫 on_expire -> teardown_display）
    if (state_ == BalloonState::Showing) {
        typewriter_.advance(dt);  // 消失後不再推進文字進度
    }
}

bool BalloonProfile::dismiss() {
    if (state_ != BalloonState::Showing) {
        return false;  // 未顯示中，no-op，不靜默
    }
    impl_->transient.expire(id_);  // 觸發 Manual 到期 -> on_expire -> teardown_display（同步）
    return true;
}

std::size_t BalloonProfile::visible_count() const {
    return state_ == BalloonState::Showing ? typewriter_.visible_count() : 0;
}

std::size_t BalloonProfile::total_count() const {
    return state_ == BalloonState::Showing ? typewriter_.total_count() : 0;
}

bool BalloonProfile::is_text_complete() const {
    return state_ == BalloonState::Showing && typewriter_.is_complete();
}

ds::render::LayoutResult BalloonProfile::render_model() const {
    if (state_ != BalloonState::Showing) {
        return {};
    }
    return typewriter_.render_model();
}

std::optional<ds::events::Tick> BalloonProfile::remaining() const {
    return impl_->transient.remaining(id_);
}

ds::kernel::AnchorStatus BalloonProfile::resolve(const ds::kernel::ResolvedPlacement& character_placement,
                                                 const ds::kernel::Size& balloon_size,
                                                 ds::kernel::ResolvedPlacement& out) const {
    return layout_.resolve_child(id_, character_placement, balloon_size, out);
}

}  // namespace ds::profiles
