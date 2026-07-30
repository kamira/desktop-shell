// E5-10 逾時計時器 — 平台中立介面
//
// 逾時計時器：設定一個逾時（以 tick 計），時間到即觸發回呼。
//   - 一次性 timeout（`set_timeout`）：到期觸發一次後自動移除。
//   - 可選重複 interval（`set_interval`）：每隔固定 tick 週期性觸發，直到取消。
//
// 本單元屬 engine 層（平台中立純邏輯），**不綁任何真實 OS 計時器 / 時鐘**：
//   - 時間以「邏輯時間 / tick」計，由呼叫端（如 E5-04 心跳）注入 `advance(dt)` / `tick()` 推進。
//   - Tick 型別沿用 E5-04（`heartbeat_source.hpp`），使 events 模組時間語意一致。
//   - 可取消（`cancel`）、可查剩餘（`remaining`）；全程可注入時間、決定性可測。
// 因此完全以單元測試驗證（注入時間、驗證觸發次數與剩餘），不依賴真實時鐘。無任何 `#ifdef`。
#ifndef DS_EVENTS_E5_10_TIMEOUT_TIMER_HPP
#define DS_EVENTS_E5_10_TIMEOUT_TIMER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "heartbeat_source.hpp"  // 沿用 ds::events::Tick（E5-04，PUBLIC 相依）

namespace ds::events {

// 計時器識別碼。由 set_timeout / set_interval 配發，單調遞增且不重用；0 保留為「無效」。
using TimerId = std::uint64_t;

// 一次逾時到期所攜帶的資料。
struct TimerEvent {
    TimerId id;            // 觸發的計時器
    Tick now;              // 觸發當下的邏輯時間
    std::uint64_t count;   // 此計時器累計第幾次觸發（自 1 起算）
    bool repeating;        // 是否為重複 interval（false = 一次性 timeout）
};

// 逾時到期時被呼叫的消費者回呼。
using TimerCallback = std::function<void(const TimerEvent&)>;

// 逾時計時器服務：管理一組一次性 / 重複計時器，隨邏輯時間推進對到期者發事件。
//
// 語意保證：
//   - 推進未達逾時不觸發；累計跨過逾時即觸發，重複計時器餘量保留至下次（不丟脈衝、不漂移）。
//   - 單次 advance(dt) 若跨越多個 interval 週期，同一重複計時器可於該次觸發多次（計次連續）。
//   - 一次性 timeout 觸發後自動移除；即使該次 advance 尚有餘量也不再觸發。
//   - 取消後不再觸發；剩餘查詢回報「距下次觸發還需幾個 tick」。
//   - 回呼在狀態更新後才被呼叫；於回呼中設定 / 取消計時器不會破壞本次派發。
class TimeoutTimer {
public:
    TimeoutTimer() = default;

    // 一次性逾時：delay（tick）後觸發 cb 一次，隨即自動移除。
    // delay 必須 > 0；否則不建立並回傳 0（無效 id）。
    TimerId set_timeout(Tick delay, TimerCallback cb);

    // 重複逾時：每 interval（tick）觸發一次 cb，直到 cancel。
    // interval 必須 > 0；否則不建立並回傳 0（無效 id）。
    TimerId set_interval(Tick interval, TimerCallback cb);

    // 取消計時器。回傳是否確有移除（未知 id 回 false）。
    bool cancel(TimerId id);

    // 推進邏輯時間 dt（tick），對每個計時器累計並觸發到期者。
    // dt 為 0 時不推進、不觸發。回傳本次總觸發次數（跨所有計時器）。
    std::size_t advance(Tick dt);

    // advance(1) 的便捷別名：推進一個 tick。
    std::size_t tick() { return advance(1); }

    // 距離該計時器下次觸發還需幾個 tick。未知 id 回 std::nullopt。
    std::optional<Tick> remaining(TimerId id) const;

    // 該 id 是否為現存（未觸發完 / 未取消）計時器。
    bool is_active(TimerId id) const noexcept;

    // 目前邏輯時間（自建構起累計推進的 tick 數）。
    Tick now() const noexcept { return now_; }

    // 現存計時器數。
    std::size_t active_count() const noexcept { return timers_.size(); }

private:
    struct Timer {
        TimerId id;
        Tick interval;         // 重複週期；一次性 timeout 為 0
        Tick remaining;        // 距下次觸發尚需的 tick（恆 > 0，於 advance 中結算）
        std::uint64_t count;   // 已觸發次數
        bool repeating;
        TimerCallback cb;
    };

    TimerId add(Tick delay, bool repeating, TimerCallback cb);

    Tick now_ = 0;
    TimerId next_id_ = 1;
    std::vector<Timer> timers_;
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_10_TIMEOUT_TIMER_HPP
