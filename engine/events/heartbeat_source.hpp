// E5-04 心跳事件 — 平台中立介面
//
// 週期性心跳 / 計時觸發：供閒置動畫、定時輪詢等消費者按固定間隔取得脈衝。
//
// 本單元屬 engine 層（平台中立純邏輯），**不綁任何真實 OS 計時器**：
//   - 時間以「邏輯時間 / tick」計，由呼叫端注入（`advance(dt)` / `tick()` 推進）。
//   - 到期的訂閱者以事件形式被通知；訂閱 / 取消訂閱皆為純資料操作。
// 因此可完全以單元測試驗證（注入時間、驗證觸發次數），不依賴真實時鐘。
#ifndef DS_EVENTS_E5_04_HEARTBEAT_SOURCE_HPP
#define DS_EVENTS_E5_04_HEARTBEAT_SOURCE_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace ds::events {

// 邏輯時間單位（tick）。呼叫端自行決定一個 tick 代表多少真實時間 —— 本單元不關心。
using Tick = std::uint64_t;

// 訂閱識別碼。由 subscribe() 配發，單調遞增且不重用；0 保留為「無效訂閱」。
using SubscriptionId = std::uint64_t;

// 一次心跳到期所攜帶的資料。
struct HeartbeatEvent {
    SubscriptionId id;     // 觸發的訂閱
    Tick interval;         // 該訂閱設定的間隔（tick）
    Tick now;              // 觸發當下的邏輯時間
    std::uint64_t count;   // 此訂閱累計第幾次觸發（自 1 起算）
};

// 心跳到期時被呼叫的消費者回呼。
using HeartbeatCallback = std::function<void(const HeartbeatEvent&)>;

// 心跳來源：管理一組週期性訂閱，隨邏輯時間推進對到期者發事件。
//
// 語意保證：
//   - 推進未達間隔不觸發；累計跨過間隔即觸發，餘量保留至下次（不丟脈衝、不漂移）。
//   - 單次 advance(dt) 若跨越多個間隔，同一訂閱可於該次觸發多次（計次連續）。
//   - 取消訂閱後不再觸發。
//   - 回呼在狀態更新後才被呼叫；於回呼中訂閱 / 取消訂閱不會破壞本次派發。
class HeartbeatSource {
public:
    HeartbeatSource() = default;

    // 訂閱：每 interval（tick）觸發一次 cb。
    // interval 必須 > 0；否則不建立訂閱並回傳 0（無效 id）。
    SubscriptionId subscribe(Tick interval, HeartbeatCallback cb);

    // 取消訂閱。回傳是否確有移除（未知 id 回 false）。
    bool unsubscribe(SubscriptionId id);

    // 推進邏輯時間 dt（tick），對每個訂閱累計並觸發到期者。
    // dt 為 0 時不推進、不觸發。回傳本次總觸發次數（跨所有訂閱）。
    std::size_t advance(Tick dt);

    // advance(1) 的便捷別名：推進一個 tick。
    std::size_t tick() { return advance(1); }

    // 目前邏輯時間（自建構起累計推進的 tick 數）。
    Tick now() const noexcept { return now_; }

    // 現存訂閱數。
    std::size_t subscription_count() const noexcept { return subs_.size(); }

private:
    struct Subscription {
        SubscriptionId id;
        Tick interval;
        Tick accumulator;      // 尚未觸發的累計時間（恆 < interval，於 advance 中結算）
        std::uint64_t count;   // 已觸發次數
        HeartbeatCallback cb;
    };

    Tick now_ = 0;
    SubscriptionId next_id_ = 1;
    std::vector<Subscription> subs_;
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_04_HEARTBEAT_SOURCE_HPP
