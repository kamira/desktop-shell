// E1-20 睡眠喚醒復原 — 實作
//
// 純事件驅動 + 記憶體快照邏輯，無平台分支、無真實電源 API。
#include "sleep_wake_recovery.hpp"

namespace ds::kernel {

SleepWakeRecovery::SleepWakeRecovery(SystemEventSource& source, LayerStack& stack,
                                      VisibilityProvider visibility)
    : source_(&source), stack_(&stack), visibility_(std::move(visibility)) {
    // 僅 SystemSleep / SystemWake 觸發本單元邏輯；其餘系統事件於 handle_event_ 內忽略。
    sub_ = source_->subscribe([this](const SystemEvent& event) { handle_event_(event); });
}

SleepWakeRecovery::~SleepWakeRecovery() {
    if (source_ != nullptr && sub_ != 0) {
        source_->unsubscribe(sub_);
    }
}

void SleepWakeRecovery::handle_event_(const SystemEvent& event) {
    if (event.type == SystemEventType::SystemSleep) {
        handle_sleep_();
    } else if (event.type == SystemEventType::SystemWake) {
        handle_wake_();
    }
    // 其餘系統事件型別（DisplayChanged / SessionLocked / ... ）：忽略，不影響快照狀態。
}

void SleepWakeRecovery::handle_sleep_() {
    // 逐一走訪目前堆疊順序（即所有已指派 surface），為每個 surface 拍一筆
    // 「具名圖層 + 可見性」快照。此快照取代前一次快照——每輪 sleep 皆是全新快照。
    std::vector<Entry> next;
    for (const SurfaceId& id : stack_->stacking_order()) {
        const SurfaceLayer* layer = stack_->layer_of(id);
        if (layer == nullptr) {
            continue;  // 理論上不可達（stacking_order 回傳的 id 必已指派）；保守略過。
        }
        const bool visible = visibility_ ? visibility_(id) : true;  // 無 provider 保守視為可見
        next.push_back(Entry{id, *layer, visible});
    }
    snapshot_ = std::move(next);
    has_snapshot_ = true;
    ++sleep_count_;
}

void SleepWakeRecovery::handle_wake_() {
    ++wake_count_;
    if (!has_snapshot_) {
        return;  // 未曾睡眠：無快照可還原（保守 no-op，不崩潰）。
    }
    for (const Entry& e : snapshot_) {
        const bool valid = validator_ ? validator_(e.id) : true;  // 無 validator 保守視為有效
        if (!valid) {
            if (on_invalidated_) {
                on_invalidated_(e.id);
            }
            continue;  // 喚醒後已失效的資源：不重建指派。
        }
        // 重建圖層指派。assign() 內部已對 kernel.surface 能力 has() 閘控（NFR-03）；
        // 能力不可用時回 RejectedNoCapability、不改動任何狀態、絕不崩潰——本單元保守
        // 略過其回傳值，讓「復原盡量做，不可用的部分安全跳過」成為自然結果。
        stack_->assign(e.id, e.layer);
        if (on_wake_) {
            on_wake_(e.id, e.layer, e.visible);
        }
    }
}

const SleepWakeRecovery::Entry* SleepWakeRecovery::find_(const SurfaceId& id) const {
    for (const Entry& e : snapshot_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

bool SleepWakeRecovery::snapshot_contains(const SurfaceId& id) const {
    return find_(id) != nullptr;
}

const SurfaceLayer* SleepWakeRecovery::snapshot_layer_of(const SurfaceId& id) const {
    const Entry* e = find_(id);
    return e != nullptr ? &e->layer : nullptr;
}

const bool* SleepWakeRecovery::snapshot_visible_of(const SurfaceId& id) const {
    const Entry* e = find_(id);
    return e != nullptr ? &e->visible : nullptr;
}

}  // namespace ds::kernel
