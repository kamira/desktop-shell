// E4-05 過場動畫與緩動 — 時間插值器（Tween / Transition）
//
// 一個 `Tween` 描述「在 duration 秒內，把數值從 from 過場到 to，沿 easing 曲線」。
// 兩種求值方式：
//   - `at(time)`：純查詢，給絕對時間（秒）回當前值，不改內部狀態。
//   - `advance(dt)`：把內部經過時間往前推 dt 秒，回推進後的當前值（有狀態，供逐幀更新）。
//
// 平台中立：時間單位是呼叫端傳入的「秒」（double），不綁任何真實時鐘 / 繪製後端 /
// 平台分支。確定性：`at(t)` 對相同 t 恆回相同值，可完全單元測試。
#ifndef DS_RENDER_E4_05_TWEEN_HPP
#define DS_RENDER_E4_05_TWEEN_HPP

#include "easing.hpp"

namespace ds::render {

// 數值過場插值器。value type 固定為 double（向量 / 顏色等由呼叫端逐分量組合）。
class Tween {
public:
    // from → to，歷時 duration 秒，沿 easing 曲線。
    // duration <= 0 視為「瞬間完成」：任何 time>=0 皆回 to、done() 恆真。
    Tween(double from, double to, double duration,
          EasingType easing = EasingType::Linear) noexcept;

    // 絕對時間查詢（純函式，不改狀態）。
    // time 會被夾到 [0, duration]，故 time<=0 回 from、time>=duration 回 to。
    double at(double time_seconds) const noexcept;

    // 把內部經過時間推進 dt 秒（dt<0 視為 0，經過時間單調不減），回推進後的當前值。
    double advance(double dt_seconds) noexcept;

    // 目前經過時間對應的值（= at(elapsed)）。
    double value() const noexcept { return at(elapsed_); }

    // 正規化進度∈[0,1]（未經 easing 的線性比例）。duration<=0 恆回 1。
    double progress() const noexcept;

    // 是否已抵達終點（elapsed >= duration）。
    bool done() const noexcept;

    // 內部經過時間歸零（可重播）。
    void reset() noexcept { elapsed_ = 0.0; }

    double from() const noexcept { return from_; }
    double to() const noexcept { return to_; }
    double duration() const noexcept { return duration_; }
    double elapsed() const noexcept { return elapsed_; }
    EasingType easing() const noexcept { return easing_; }

private:
    double from_;
    double to_;
    double duration_;
    EasingType easing_;
    double elapsed_ = 0.0;
};

}  // namespace ds::render

#endif  // DS_RENDER_E4_05_TWEEN_HPP
