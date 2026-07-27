// E5-09 機率排程器 — 平台中立介面
//
// 以「機率」在時間推進中觸發事件：供桌面角色的隨機行為（如隨機閒置動作 —— 眨眼、
// 伸懶腰、換姿勢等）在時間流逝中偶發地發生，而非每拍必觸發。
//
// 本單元屬 engine 層（平台中立純邏輯）：
//   - 時間以「邏輯時間 / tick」計，由呼叫端注入（`advance(dt)` / `tick()` 推進）。
//     時間推進委派給已合併的 E5-04 `HeartbeatSource`（PUBLIC 相依）——每個機率任務
//     背後掛一個心跳訂閱，心跳到期即為一次「評估機會」。
//   - 每次評估機會抽一個可注入亂數；抽值 < 該任務機率則觸發回呼。
//   - **RNG 可注入 / 可替換**（見 `random_source.hpp`），固定種子下觸發序列完全決定性、
//     可於單元測試重現；不使用全域 `rand()`。
//
// 因此可完全以單元測試驗證（注入種子 / 假亂數、驗證觸發序列與次數），不依賴真實時鐘或熵。
#ifndef DS_EVENTS_E5_09_PROBABILISTIC_SCHEDULER_HPP
#define DS_EVENTS_E5_09_PROBABILISTIC_SCHEDULER_HPP

#include "heartbeat_source.hpp"  // E5-04：Tick / SubscriptionId / HeartbeatSource
#include "random_source.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace ds::events {

// 一次機率觸發所攜帶的資料。
struct ScheduledEvent {
    SubscriptionId id;          // 觸發的排程任務
    Tick interval;              // 該任務的評估間隔（tick）
    double probability;         // 該任務每次評估的觸發機率（已夾至 [0,1]）
    Tick now;                   // 觸發當下的邏輯時間
    std::uint64_t eval_count;   // 此任務累計第幾次「評估機會」（自 1 起算）
    std::uint64_t fire_count;   // 此任務累計第幾次「觸發」（自 1 起算，含本次）
};

// 機率觸發時被呼叫的消費者回呼。
using ScheduledCallback = std::function<void(const ScheduledEvent&)>;

// 機率排程器：管理一組「以機率、按間隔評估」的任務，隨邏輯時間推進偶發觸發。
//
// 語意保證：
//   - 每經過 interval（tick）為該任務一次「評估機會」；每次評估**固定抽一個亂數** u∈[0,1)，
//     u < probability 即觸發（故 probability=0 永不觸發、probability>=1 每次評估必觸發）。
//   - 「每次評估抽剛好一個亂數」與心跳到期順序共同決定亂數消耗次序 —— 固定種子下完全決定性。
//   - 單次 advance(dt) 若跨越某任務多個間隔，該任務於本次可有多次評估（各自獨立抽值）。
//   - 取消任務後不再評估 / 觸發。
//   - 回呼在狀態更新後才被呼叫；於回呼中新增 / 取消任務不會破壞本次派發。
class ProbabilisticScheduler {
public:
    // 以固定種子建構（預設 0）：內建 SeededRandomSource，序列完全由種子決定。
    explicit ProbabilisticScheduler(std::uint64_t seed = 0)
        : rng_(std::make_shared<SeededRandomSource>(seed)) {}

    // 注入自訂亂數來源（測試假來源 / 產品自備 PRNG 皆可）。rng 為空則退回種子 0 的預設來源。
    explicit ProbabilisticScheduler(std::shared_ptr<RandomSource> rng)
        : rng_(rng ? std::move(rng) : std::make_shared<SeededRandomSource>(0)) {}

    // 排程一個機率任務：每 interval（tick）評估一次，以 probability 觸發 cb。
    // interval 必須 > 0；否則不建立並回傳 0（無效 id）。probability 會夾至 [0,1]。
    SubscriptionId schedule(Tick interval, double probability, ScheduledCallback cb);

    // 取消排程。回傳是否確有移除（未知 id 回 false）。
    bool cancel(SubscriptionId id);

    // 推進邏輯時間 dt（tick），對每個任務按間隔評估並機率觸發。
    // dt 為 0 時不推進、不評估。回傳本次實際觸發次數（跨所有任務）。
    std::size_t advance(Tick dt);

    // advance(1) 的便捷別名：推進一個 tick。
    std::size_t tick() { return advance(1); }

    // 目前邏輯時間（自建構起累計推進的 tick 數）。
    Tick now() const noexcept { return heartbeat_.now(); }

    // 現存排程任務數。
    std::size_t task_count() const noexcept { return tasks_.size(); }

    // 替換亂數來源（如測試中重設序列）。空指標忽略。之後的評估改用新來源。
    void set_random_source(std::shared_ptr<RandomSource> rng) {
        if (rng) {
            rng_ = std::move(rng);
        }
    }

private:
    struct Task {
        Tick interval;
        double probability;
        std::uint64_t eval_count;   // 累計評估次數
        std::uint64_t fire_count;   // 累計觸發次數
        ScheduledCallback cb;
    };

    // 心跳到期時的評估：抽亂數、判斷觸發、更新計數並收集待派發事件。
    void evaluate(SubscriptionId id, Tick now);

    std::shared_ptr<RandomSource> rng_;
    HeartbeatSource heartbeat_;                       // E5-04：邏輯時間推進來源
    std::unordered_map<SubscriptionId, Task> tasks_;  // 以心跳訂閱 id 為鍵
    std::size_t fires_this_advance_ = 0;              // advance 期間累計觸發數
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_09_PROBABILISTIC_SCHEDULER_HPP
