// E4-09 動畫驅動源 — 實作
//
// 純邏輯：把 E5-04 心跳的脈衝轉為對各註冊動畫的時間推進，不含任何平台分支或真實計時器。
#include "animation_driver.hpp"

#include <utility>

namespace ds::render {

AnimationDriver::AnimationDriver(ds::events::HeartbeatSource& hb, Tick pulse_interval)
    : hb_(hb), pulse_interval_(pulse_interval == 0 ? 1 : pulse_interval) {
    // 對心跳來源建立一筆訂閱：每 pulse_interval（tick）脈衝一次，轉呼 on_pulse。
    sub_id_ = hb_.subscribe(pulse_interval_,
                            [this](const ds::events::HeartbeatEvent& ev) { on_pulse(ev); });
}

AnimationDriver::~AnimationDriver() {
    if (sub_id_ != 0) {
        hb_.unsubscribe(sub_id_);
    }
}

AnimationId AnimationDriver::add(AnimationCallback cb) {
    if (!cb) {
        return kInvalidAnimationId;  // 空回呼：不註冊
    }
    const AnimationId id = next_id_++;
    anims_.push_back(Animation{id, /*paused=*/false, /*elapsed=*/0, /*frame=*/0, std::move(cb)});
    return id;
}

bool AnimationDriver::remove(AnimationId id) {
    for (auto it = anims_.begin(); it != anims_.end(); ++it) {
        if (it->id == id) {
            anims_.erase(it);
            return true;
        }
    }
    return false;
}

bool AnimationDriver::pause(AnimationId id) {
    for (auto& a : anims_) {
        if (a.id == id) {
            if (a.paused) {
                return false;  // 已暫停
            }
            a.paused = true;
            return true;
        }
    }
    return false;
}

bool AnimationDriver::resume(AnimationId id) {
    for (auto& a : anims_) {
        if (a.id == id) {
            if (!a.paused) {
                return false;  // 已在運行
            }
            a.paused = false;
            return true;
        }
    }
    return false;
}

bool AnimationDriver::is_paused(AnimationId id) const noexcept {
    for (const auto& a : anims_) {
        if (a.id == id) {
            return a.paused;
        }
    }
    return false;
}

void AnimationDriver::on_pulse(const ds::events::HeartbeatEvent& ev) {
    if (global_paused_) {
        return;  // 驅動源整體暫停：本次脈衝不推進任何動畫
    }
    const Tick dt = ev.interval;  // 一次脈衝的時間增量即心跳間隔

    // 兩階段派發：先結算所有動畫狀態並收集待呼叫的幀，再統一觸發回呼。
    // 如此回呼中若新增 / 移除 / 暫停動畫，也不會使正在遍歷的 anims_ 失效。
    struct Pending {
        AnimationCallback cb;
        AnimationFrame frame;
    };
    std::vector<Pending> pending;

    for (auto& a : anims_) {
        if (a.paused) {
            continue;  // 暫停中：不餵 dt、不累計
        }
        a.elapsed += dt;
        ++a.frame;
        pending.push_back(Pending{a.cb, AnimationFrame{a.id, dt, a.elapsed, a.frame}});
    }

    for (auto& p : pending) {
        if (p.cb) {
            p.cb(p.frame);
        }
    }
}

}  // namespace ds::render
