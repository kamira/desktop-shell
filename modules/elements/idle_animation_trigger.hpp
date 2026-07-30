// E4-10 閒置自動動畫觸發 — 平台中立介面（module 層 / elements 子系統）
//
// 語意：桌面角色在「一段時間沒有互動」後，該偶發地自己做點動作（如眨眼、換姿勢等），
// 而不是死板地保持靜止。本單元把兩個已合併的上游組起來完成這件事：
//   - 以**注入式** tick 累計「距上次活動已過多久」，累計滿所設閒置門檻即視為「閒置中」；
//   - 唯閒置中才把 tick 餵給上游 **E5-09** `ProbabilisticScheduler`，由其以機率決定「這次
//     評估機會要不要觸發」；一觸發即對綁定的上游 **E4-07** `FrameAnimationElement` 執行
//     `reset()` + `play()`（從頭播放一次幀序列動畫）。E5-04 心跳文件本就點名「閒置動畫」
//     為其消費場景之一（見 `heartbeat_source.hpp`），本單元即該場景的具體組裝。
//
// 分工邊界（避免與上游重疊）：
//   - **不**自行推進動畫的逐幀進度——`FrameAnimationElement::advance()` / `attach()` 由呼叫端
//     另行接線（如接到 E4-09 動畫驅動源）；本單元只在「該不該觸發」這件事上做決定。
//   - **不**偵測什麼是「互動」——由呼叫端偵測滑鼠 / 鍵盤 / 其他輸入事件後呼叫
//     `notify_activity()`；本單元只管「多久沒被通知活動 = 閒置」。
//
// 相位 1：時間全為注入式 tick（沿用 E5-09 / E5-04 的 `Tick = std::uint64_t`），不碰真實時鐘 /
// 計時器；平台中立、無 `#ifdef` / win32 / cocoa。
//
// 決定性：機率判斷委由 E5-09（可注入 `RandomSource`，固定種子下觸發序列完全決定性、可重現，
// 承其單元測試精神）；且**未達閒置門檻前完全不推進排程器**，不消耗任何亂數 —— 亂數消耗次序
// 只由「真正進入閒置後的評估次數」決定，維持可預期、可重現。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_10_IDLE_ANIMATION_TRIGGER_HPP
#define DS_ELEMENTS_E4_10_IDLE_ANIMATION_TRIGGER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>

#include "frame_animation_element.hpp"  // E4-07（上游，可讀不可改）：FrameAnimationElement / Tick
#include "probabilistic_scheduler.hpp"  // E5-09（上游，可讀不可改）：ProbabilisticScheduler / RandomSource

namespace ds::elements {

// 註冊一個「候選閒置動畫」時配發的識別碼；直接沿用 E5-09 排程任務 id（同一枚舉域），
// 0 保留為「無效」（如 `register_animation` 的 eval_interval <= 0 時）。
using RegistrationId = ds::events::SubscriptionId;

// 操作結果碼——與同子系統各 module 層單元同精神：明確、不靜默。
enum class IdleAnimationStatus {
    Ok,       // 操作成功
    Invalid,  // 前置條件違反（如閒置門檻為 0）；不套用
};

// -----------------------------------------------------------------------------
// IdleAnimationTrigger —— 閒置自動動畫觸發器。
//
// 用法：呼叫端持續以 `advance(dt)` 注入邏輯時間；有真實互動發生時呼叫
// `notify_activity()` 重置閒置計時；以 `register_animation()` 登記一或多個候選幀序列動畫
// （各自獨立的評估間隔 + 機率）。累計閒置時間達門檻後，登記的動畫才開始被 E5-09 排程器評估，
// 機率命中即觸發（`reset()` + `play()`）。
//
// 生命週期：本物件持有 `ProbabilisticScheduler` 成員，其排程任務的回呼閉包內部參照 `this`
// （轉呼本物件狀態）—— 故本物件**不可複製 / 不可移動**（承 E4-09 `AnimationDriver` 同類自參照
// 理由）。`register_animation` 所綁的 `FrameAnimationElement&` 由呼叫端持有所有權，其壽命須
// 涵蓋本物件解構或 `unregister_animation()` 之前。
// -----------------------------------------------------------------------------
class IdleAnimationTrigger {
public:
    // 以固定種子建構（預設 0）：內建 SeededRandomSource，序列完全由種子決定。
    // idle_threshold：閒置門檻（tick），須 > 0；傳入 0 視為未設定，退回預設 1（不靜默拒建構，
    // 以最保守的最小門檻代之——之後可用 `set_idle_threshold` 明確重設）。
    explicit IdleAnimationTrigger(Tick idle_threshold = 1, std::uint64_t seed = 0)
        : idle_threshold_(idle_threshold > 0 ? idle_threshold : 1), scheduler_(seed) {}

    // 注入自訂亂數來源（測試假來源 / 產品自備 PRNG 皆可）。
    IdleAnimationTrigger(Tick idle_threshold, std::shared_ptr<ds::events::RandomSource> rng)
        : idle_threshold_(idle_threshold > 0 ? idle_threshold : 1), scheduler_(std::move(rng)) {}

    IdleAnimationTrigger(const IdleAnimationTrigger&) = delete;
    IdleAnimationTrigger& operator=(const IdleAnimationTrigger&) = delete;
    IdleAnimationTrigger(IdleAnimationTrigger&&) = delete;
    IdleAnimationTrigger& operator=(IdleAnimationTrigger&&) = delete;

    // 設定閒置門檻（tick）：累計距上次活動滿此 tick 數才視為「閒置中」。
    // 0 → `Invalid`（不套用，維持既有門檻）。
    IdleAnimationStatus set_idle_threshold(Tick threshold) noexcept;
    Tick idle_threshold() const noexcept { return idle_threshold_; }

    // 登記一個候選閒置動畫：閒置中每 eval_interval（tick）評估一次，以 probability 觸發
    // （對 animation 執行 `reset()` + `play()`，即從頭播放一次）。eval_interval 必須 > 0，
    // 否則不建立並回傳 0（無效 id）；probability 由 E5-09 夾至 [0,1]（見其文件，越界值不報錯、
    // 安全夾限）。animation 須存活至本物件解構或對應 `unregister_animation()` 之前——本單元
    // 不持有其所有權，僅持參照透過回呼閉包呼叫（承 E4-09 `AnimationDriver::add` 同類慣例）。
    RegistrationId register_animation(FrameAnimationElement& animation, Tick eval_interval,
                                       double probability);

    // 取消登記。回傳是否確有移除（未知 id 回 false）。
    bool unregister_animation(RegistrationId id) { return scheduler_.cancel(id); }

    // 現存登記動畫數。
    std::size_t animation_count() const noexcept { return scheduler_.task_count(); }

    // 通知一次真實活動發生：閒置計時歸零（回到「非閒置」）。已登記動畫的觸發評估隨之暫停，
    // 直到再次累計滿閒置門檻；不影響已在播放中的動畫（不主動停止 / 不重置其播放狀態）。
    void notify_activity() noexcept { idle_elapsed_ = 0; }

    // 以邏輯時間增量 dt（tick）推進：
    //   - 累計閒置時間 `idle_elapsed_ += dt`；
    //   - 唯累計後達門檻（`idle_elapsed_ >= idle_threshold_`，即「閒置中」）才把本次 dt 餵給
    //     E5-09 排程器評估各登記動畫；未達門檻前排程器完全不推進、不消耗任何亂數（維持決定性
    //     可重現，序列只由「真正進入閒置後」的評估次數決定）；
    //   - dt 為 0 時安全 no-op（不推進、不評估）。
    // 回傳本次實際觸發次數（跨所有登記動畫）。
    std::size_t advance(Tick dt);

    // 目前累計閒置時間（tick，自建構或最近一次 `notify_activity()` 起算）。
    Tick idle_elapsed() const noexcept { return idle_elapsed_; }

    // 是否處於「閒置中」（`idle_elapsed() >= idle_threshold()`）。
    bool is_idle() const noexcept { return idle_elapsed_ >= idle_threshold_; }

    // 累計總觸發次數（自建構起，跨所有登記動畫）。
    std::uint64_t total_trigger_count() const noexcept { return total_triggers_; }

private:
    Tick idle_threshold_;
    Tick idle_elapsed_ = 0;
    ds::events::ProbabilisticScheduler scheduler_;
    std::uint64_t total_triggers_ = 0;
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_10_IDLE_ANIMATION_TRIGGER_HPP
