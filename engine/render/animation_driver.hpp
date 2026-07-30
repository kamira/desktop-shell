// E4-09 動畫驅動源 — 平台中立介面
//
// 為動畫提供「時間推進」的驅動來源：由 E5-04 心跳（`ds::events::HeartbeatSource`）
// 推進，把每次脈衝的時間增量（dt，tick）餵給註冊的動畫回呼，供 tween / 幀動畫消費。
//
// 本單元屬 engine 層（平台中立純邏輯），**不綁任何真實 OS 計時器 / 繪製後端**：
//   - 時間以「邏輯時間 / tick」計；驅動源本身不推進時間，一切由所綁的心跳來源
//     `advance(dt)` / `tick()` 觸發（見 E5-04）。
//   - 一次心跳脈衝 → 對每個「未暫停」的動畫餵一份 dt（= 該訂閱的心跳間隔），
//     並累計該動畫的經過時間與幀序。單次 advance 若跨多個心跳間隔，心跳會多次脈衝，
//     驅動源即多次派發（不丟脈衝、不漂移，語意承自 E5-04）。
//   - 動畫可暫停 / 繼續 / 移除；暫停期間不餵 dt、不累計經過時間與幀序。
// 因此可完全以單元測試驗證（注入邏輯時間、斷言各動畫被推進的次數與累計量），不依賴真實時鐘。
#ifndef DS_RENDER_E4_09_ANIMATION_DRIVER_HPP
#define DS_RENDER_E4_09_ANIMATION_DRIVER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "heartbeat_source.hpp"  // E5-04（相依上游，可讀不可改）

namespace ds::render {

// 邏輯時間單位（tick），沿用 E5-04 的定義。
using Tick = ds::events::Tick;

// 動畫識別碼。由 add() 配發，單調遞增且不重用；0 保留為「無效動畫」。
using AnimationId = std::uint64_t;
inline constexpr AnimationId kInvalidAnimationId = 0;

// 一次驅動脈衝餵給某動畫的資料。
struct AnimationFrame {
    AnimationId id;         // 被推進的動畫
    Tick dt;                // 本次脈衝的時間增量（tick）
    Tick elapsed;           // 該動畫自註冊以來累計推進時間（不含暫停期間）
    std::uint64_t frame;    // 此動畫累計第幾次被推進（自 1 起算）
};

// 動畫被驅動時呼叫的消費者回呼：拿到 dt 即可推進 tween / 幀動畫。
using AnimationCallback = std::function<void(const AnimationFrame&)>;

// 動畫驅動源：綁定一個心跳來源，將心跳脈衝轉為對各註冊動畫的時間推進。
//
// 語意保證：
//   - 每次心跳脈衝，對每個「未暫停且驅動源整體未暫停」的動畫餵一份 dt（= 心跳間隔），
//     並使其 elapsed += dt、frame += 1。
//   - 暫停中的動畫（或驅動源整體暫停）在脈衝時被略過：不餵 dt、不累計。
//   - 移除的動畫不再被推進。
//   - 回呼在狀態更新後才被呼叫；於回呼中新增 / 移除 / 暫停動畫不會破壞本次派發。
//
// 生命週期：建構時對心跳來源建立一筆訂閱，解構時自動解除。驅動源持有 `this` 於訂閱閉包中，
// 故不可複製 / 不可移動（避免懸空指標）；所綁的心跳來源之壽命須涵蓋本驅動源。
class AnimationDriver {
public:
    // 綁定心跳來源 hb，以 pulse_interval（tick）為驅動節拍向其訂閱。
    // pulse_interval 會被夾到至少 1（心跳不接受間隔 0）。
    explicit AnimationDriver(ds::events::HeartbeatSource& hb, Tick pulse_interval = 1);
    ~AnimationDriver();

    AnimationDriver(const AnimationDriver&) = delete;
    AnimationDriver& operator=(const AnimationDriver&) = delete;
    AnimationDriver(AnimationDriver&&) = delete;
    AnimationDriver& operator=(AnimationDriver&&) = delete;

    // 註冊一個動畫回呼，回傳其 id。空回呼不註冊並回傳 0（無效 id）。
    AnimationId add(AnimationCallback cb);

    // 移除某動畫。回傳是否確有移除（未知 id 回 false）。
    bool remove(AnimationId id);

    // 暫停 / 繼續某動畫。回傳是否有狀態改變（未知 id，或已是該狀態時回 false）。
    bool pause(AnimationId id);
    bool resume(AnimationId id);

    // 查詢某動畫是否暫停中（未知 id 回 false）。
    bool is_paused(AnimationId id) const noexcept;

    // 驅動源整體暫停 / 繼續：暫停時任何脈衝都不推進任何動畫。
    void pause_all() noexcept { global_paused_ = true; }
    void resume_all() noexcept { global_paused_ = false; }
    bool paused() const noexcept { return global_paused_; }

    // 現存動畫數。
    std::size_t animation_count() const noexcept { return anims_.size(); }

    // 驅動節拍（每次脈衝餵出的 dt，tick）。
    Tick pulse_interval() const noexcept { return pulse_interval_; }

private:
    // 心跳到期時被呼叫：把一次脈衝的 dt 派發給各未暫停動畫。
    void on_pulse(const ds::events::HeartbeatEvent& ev);

    struct Animation {
        AnimationId id;
        bool paused;
        Tick elapsed;           // 累計推進時間（不含暫停期間）
        std::uint64_t frame;    // 已推進次數
        AnimationCallback cb;
    };

    ds::events::HeartbeatSource& hb_;
    ds::events::SubscriptionId sub_id_ = 0;
    Tick pulse_interval_;
    AnimationId next_id_ = 1;
    bool global_paused_ = false;
    std::vector<Animation> anims_;
};

}  // namespace ds::render

#endif  // DS_RENDER_E4_09_ANIMATION_DRIVER_HPP
