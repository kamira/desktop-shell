// E1-14 短暫 profile 生命週期 — 實作（相位 1 = null，純記憶體邏輯）
//
// 見 transient_profile.hpp 的設計說明。時間全走 E5-10 注入式 tick，無 wall-clock；
// 無平台分支、無真實 OS 呼叫。
#include "transient_profile.hpp"

#include <utility>

namespace ds::kernel {

const char* to_string(ExpiryReason r) noexcept {
    switch (r) {
        case ExpiryReason::Timeout: return "timeout";
        case ExpiryReason::Manual:  return "manual";
    }
    return "unknown";
}

TransientProfileManager::TransientProfileManager(ds::events::TimeoutTimer& timer) noexcept
    : timer_(timer) {}

TransientProfileManager::~TransientProfileManager() {
    // 取消本管理器登記於注入計時器上的所有未觸發計時器，避免懸置捕捉 this 的回呼。
    for (const Entry& e : entries_) {
        timer_.cancel(e.timer);
    }
}

TransientProfileManager::Entry* TransientProfileManager::find(const TransientId& id) {
    for (Entry& e : entries_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const TransientProfileManager::Entry* TransientProfileManager::find(const TransientId& id) const {
    for (const Entry& e : entries_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

bool TransientProfileManager::create(const TransientId& id,
                                     const TransientProfile& profile,
                                     ds::events::Tick ttl) {
    // 保守前置：空 id / ttl 為 0 / id 已存活 → 拒絕，不靜默覆寫。
    if (id.empty() || ttl == 0 || find(id) != nullptr) {
        return false;
    }

    // 登記一次性逾時：到期即自動過期並觸發 on_expire(Timeout)。
    // 此時該計時器已由 E5-10 自動移除（一次性），故回呼內無需 cancel。
    const ds::events::TimerId tid =
        timer_.set_timeout(ttl, [this, id](const ds::events::TimerEvent&) {
            if (fire_expiry(id, ExpiryReason::Timeout)) {
                ++advance_timeout_count_;
            }
        });
    if (tid == 0) {
        // 理論上 ttl > 0 必得有效 id；防禦性處理，不建立殘缺記錄。
        return false;
    }

    Entry entry;
    entry.id = id;
    entry.profile = profile;
    entry.profile.surface.lifecycle = SurfaceLifecycle::Ephemeral;  // 強制短暫語意。
    entry.timer = tid;
    entries_.push_back(std::move(entry));
    return true;
}

std::size_t TransientProfileManager::advance(ds::events::Tick ticks) {
    advance_timeout_count_ = 0;
    timer_.advance(ticks);  // 到期者於此同步觸發回呼 → fire_expiry(Timeout)。
    return advance_timeout_count_;
}

bool TransientProfileManager::expire(const TransientId& id) {
    Entry* e = find(id);
    if (e == nullptr) {
        return false;  // 未知 / 已過期：明確不靜默。
    }
    // 提早結束：先取消其逾時計時器（避免稍後又逾時觸發一次），再過期收尾。
    timer_.cancel(e->timer);
    fire_expiry(id, ExpiryReason::Manual);
    return true;
}

void TransientProfileManager::on_expire(ExpiryCallback cb) {
    on_expire_.push_back(std::move(cb));
}

bool TransientProfileManager::is_alive(const TransientId& id) const {
    return find(id) != nullptr;
}

std::optional<ds::events::Tick> TransientProfileManager::remaining(const TransientId& id) const {
    const Entry* e = find(id);
    if (e == nullptr) {
        return std::nullopt;
    }
    return timer_.remaining(e->timer);
}

const TransientProfile* TransientProfileManager::profile(const TransientId& id) const {
    const Entry* e = find(id);
    return e ? &e->profile : nullptr;
}

InputStrategy TransientProfileManager::input_strategy(const TransientId& id) const {
    const Entry* e = find(id);
    return e ? e->profile.input : kDefaultStrategy;
}

bool TransientProfileManager::fire_expiry(const TransientId& id, ExpiryReason reason) {
    // 先移除記錄（清理），使回呼內 is_alive(id) == false（收尾一致視圖）。
    bool removed = false;
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->id == id) {
            entries_.erase(it);
            removed = true;
            break;
        }
    }
    if (!removed) {
        return false;  // 已不存活（理論上不會走到；防禦）。
    }
    // 依登記序呼叫過期回呼。以初始快照大小遍歷，避免回呼內再登記造成的失效。
    const std::size_t n = on_expire_.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (on_expire_[i]) {
            on_expire_[i](id, reason);
        }
    }
    return true;
}

}  // namespace ds::kernel
