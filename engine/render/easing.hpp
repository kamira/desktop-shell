// E4-05 過場動畫與緩動 — 平台中立純邏輯
//
// 提供兩件事：
//   1. `Easing`：一組緩動曲線函式，輸入正規化進度 t∈[0,1]，回傳 eased 值。
//   2. `Tween`：起訖值 + 時長 + 緩動曲線的時間插值器，可用絕對時間 `at(t)`
//      或增量推進 `advance(dt)` 求當前值。
//
// engine 層 / 繪製基座：**純數學**，不綁真實時鐘、不綁繪製後端、無平台分支
//（無 `#ifdef _WIN32` / win32 / cocoa）。時間一律以呼叫端傳入的「秒」表示，
// 由呼叫端決定時間來源，本單元只做確定性插值 —— 給相同輸入必得相同輸出，
// 因此可完全單元測試（給 t 驗值）。
#ifndef DS_RENDER_E4_05_EASING_HPP
#define DS_RENDER_E4_05_EASING_HPP

namespace ds::render {

// 緩動曲線種類。每種都是 [0,1] → 值 的映射，且滿足 f(0)=0、f(1)=1。
//
// 命名慣例（業界通用）：
//   In    = 慢入快出（開頭平緩）
//   Out   = 快入慢出（結尾平緩）
//   InOut = 兩端平緩、中段快
enum class EasingType {
    Linear,      // 等速
    InQuad,      // 二次
    OutQuad,
    InOutQuad,
    InCubic,     // 三次
    OutCubic,
    InOutCubic,
    InSine,      // 正弦
    OutSine,
    InOutSine,
};

// 一組緩動函式。全部為靜態純函式，無狀態。
//
// **所有函式都會先把 t 夾到 [0,1]**，因此傳入越界值（例如 advance 過頭）永遠安全：
// t<0 視為 0、t>1 視為 1。這保證邊界精確（f(0)=0、f(1)=1）且輸出恆在合理域。
struct Easing {
    // 把 t 夾到 [0,1]。
    static double clamp01(double t) noexcept;

    // 各曲線：輸入 t∈[0,1]（會先 clamp），回傳 eased 進度∈[0,1]。
    static double linear(double t) noexcept;
    static double in_quad(double t) noexcept;
    static double out_quad(double t) noexcept;
    static double in_out_quad(double t) noexcept;
    static double in_cubic(double t) noexcept;
    static double out_cubic(double t) noexcept;
    static double in_out_cubic(double t) noexcept;
    static double in_sine(double t) noexcept;
    static double out_sine(double t) noexcept;
    static double in_out_sine(double t) noexcept;

    // 依 type 分派到對應曲線。未知列舉回退為 linear（保守、不丟例外）。
    static double apply(EasingType type, double t) noexcept;
};

}  // namespace ds::render

#endif  // DS_RENDER_E4_05_EASING_HPP
