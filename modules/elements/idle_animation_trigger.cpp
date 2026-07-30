// E4-10 閒置自動動畫觸發 — 實作
//
// 純邏輯：閒置計時本身以注入式 tick 累計；達門檻前完全不呼叫 E5-09 排程器的 advance（不消耗
// 任何亂數），達門檻後才把 dt 轉呼排程器評估。觸發時對綁定的 E4-07 動畫執行 reset()+play()。
// 不含任何平台分支或真實計時器 / 熵源。
#include "idle_animation_trigger.hpp"

namespace ds::elements {

IdleAnimationStatus IdleAnimationTrigger::set_idle_threshold(Tick threshold) noexcept {
    if (threshold == 0) {
        return IdleAnimationStatus::Invalid;  // 門檻 0 無意義（恆閒置）：不套用，不靜默
    }
    idle_threshold_ = threshold;
    return IdleAnimationStatus::Ok;
}

RegistrationId IdleAnimationTrigger::register_animation(FrameAnimationElement& animation,
                                                          Tick eval_interval,
                                                          double probability) {
    FrameAnimationElement* anim = &animation;
    // 觸發動作：從頭播放一次（reset 回第 0 幀後 play）。閉包參照 this 以更新總觸發計數——
    // 本物件已刪除複製 / 搬移，故 this 恆穩定。
    return scheduler_.schedule(eval_interval, probability,
                                [this, anim](const ds::events::ScheduledEvent&) {
                                    anim->reset();
                                    anim->play();
                                    ++total_triggers_;
                                });
}

std::size_t IdleAnimationTrigger::advance(Tick dt) {
    if (dt == 0) {
        return 0;  // 安全 no-op：不推進、不評估
    }
    idle_elapsed_ += dt;
    if (idle_elapsed_ < idle_threshold_) {
        return 0;  // 尚未達閒置門檻：排程器完全不推進，不消耗任何亂數（維持決定性可重現）
    }
    return scheduler_.advance(dt);
}

}  // namespace ds::elements
