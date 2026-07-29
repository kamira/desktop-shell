// E1-15 視窗級透明度與懸停淡變 — 平台中立介面（platform 相位 1 = Mac / null 期）
//
// 語意：整個視窗 / surface 的**整體透明度**（沿用上游 E1-03 `AlphaProfile::opacity` 的比例
// 語意，[0,1]，NFR-02：比例非座標）——設定一個「未懸停」時的基準不透明度，滑鼠懸停時
// **淡入**（往懸停目標不透明度過場，通常更不透明）、離開時**淡出**（往基準不透明度過場，
// 通常更半透明）。淡變由上游 E5-02 `HoverTracker` 的 Enter / Leave 懸停事件驅動起停，
// 實際推進靠呼叫端**注入式時間** `advance(dt)`（本單元不綁任何真實時鐘 / OS 計時器）。
//
// 建於上游之上（可讀不可改）：
//   - E1-03 `AlphaSurfaceService` / `AlphaProfile` / `AlphaStatus`：本單元透過既有服務把
//     計算出的目前不透明度實際套用到具名 surface；能力閘控（NFR-03）與 opacity 正規化
//     皆已由 E1-03 負責，本單元只轉發其回報，不重複驗證邏輯。
//   - E5-02 `HoverTracker` / `HoverEvent` / `HoverEventKind`：本單元訂閱其懸停事件，僅回應
//     與本 surface 同 id 的 Enter / Leave（其餘事件 / 其他 surface 忽略），驅動淡變目標。
//
// 相位 1 硬約束：無 `#ifdef` / `win32` / `cocoa` / 真實 OS 時鐘；時間一律注入式（呼叫端 /
// 測試以 `advance(dt_seconds)` 推進，dt 為呼叫端提供的「秒」，非真實牆鐘）。
//
// 無效透明度處理不靜默：`set_base_opacity` / `set_hover_opacity` 遇非有限值（NaN / Inf）
// 一律回 `WindowOpacityStatus::Invalid`、**不套用**（不悄悄 clamp 成別的值生效）；有限值則
// 依 NFR-02（透明度為比例）clamp 至 [0,1] 後套用——與上游 E1-03 `AlphaProfile::opacity`
// 的正規化規則一致。
#ifndef DS_KERNEL_E1_15_WINDOW_OPACITY_HPP
#define DS_KERNEL_E1_15_WINDOW_OPACITY_HPP

#include "alpha_surface.hpp"  // E1-03（上游，可讀不可改）：AlphaSurfaceService / AlphaStatus
#include "hover_tracker.hpp"  // E5-02（上游，可讀不可改）：HoverTracker / HoverEvent / SubscriptionId

namespace ds::kernel {

// 操作結果碼 —— 與上游 E1-03 `AlphaStatus` 同語意，直接轉發（本單元不重複定義新語意）。
enum class WindowOpacityStatus {
    Ok,           // 操作成功並已套用
    Invalid,      // 前置條件不滿足（opacity 非有限值、surface 未於 E1-03 註冊為 alpha surface 等）
    Unsupported,  // per-pixel alpha 能力於綁定後端不可用（NFR-03，轉發自 E1-03 `supported()`）
};

// ---------------------------------------------------------------------------
// WindowOpacity —— 視窗 / surface 級整體透明度 + 懸停淡變。
//
// 組合持有上游 E1-03 `AlphaSurfaceService&`（不取得所有權；本單元透過它把計算出的目前
// 不透明度實際套用到 `surface`）。
//
// 語意保證：
//   - `set_base_opacity(opacity)` / `set_hover_opacity(opacity)`：設定「未懸停」/「懸停中」
//     的目標不透明度比例。非有限值 → Invalid，不套用；有限值 clamp 至 [0,1]。若目前狀態
//     正處於該目標所屬情境（未懸停時改基準、懸停中改懸停目標），會**立即**（無動畫）把
//     目前不透明度設為新值並同步套用；否則僅記錄，供下次對應懸停事件觸發時使用。
//   - `attach(hover)` 訂閱一個 E5-02 `HoverTracker`：本 surface 的 Enter → 淡入（往
//     `hover_opacity()` 過場）；本 surface 的 Leave → 淡出（往 `base_opacity()` 過場）；
//     其他 surface 的事件、Move 事件一律忽略。回傳訂閱代號（0 = 訂閱失敗）。
//   - `advance(dt_seconds)`：把內部淡變經過時間推進 dt 秒（dt<0 視為 0），依 `fade_seconds`
//     線性插值出目前不透明度，並套用至綁定的 alpha surface；回傳套用結果（轉發自 E1-03）。
//     `fade_seconds<=0` 視為瞬時切換（不需 advance 亦可由下一次 advance 立即抵達目標）。
//   - `current_opacity()`：純查詢目前（動畫中）不透明度比例 [0,1]，不改狀態、不重新套用。
// ---------------------------------------------------------------------------
class WindowOpacity {
public:
    // surface：須為已由 E1-03 `AlphaSurfaceService::create_alpha_surface` 建立的具名 alpha
    // surface（本類別不建立 / 銷毀 surface，只設定其 opacity）。
    // alpha_service：E1-03 服務（本類別透過它實際套用 opacity；不取得所有權，須存活於本
    // 物件之外的生命週期內）。
    // fade_seconds：淡入 / 淡出動畫歷時（秒）。<=0 視為瞬時切換（無過場）。
    explicit WindowOpacity(AlphaSurfaceService& alpha_service, SurfaceId surface,
                           double fade_seconds = 0.2);

    // 設定「未懸停」時的基準不透明度比例 [0,1]。非有限值 → Invalid、不套用。
    // 若目前未懸停，立即（無動畫）套用為目前不透明度；若目前正懸停中，僅記錄供之後淡出使用。
    WindowOpacityStatus set_base_opacity(float opacity);

    // 設定「懸停中」時的目標不透明度比例 [0,1]。語意同上，惟「立即套用」條件為目前正懸停中。
    WindowOpacityStatus set_hover_opacity(float opacity);

    // 訂閱 E5-02 HoverTracker 的懸停事件：本 surface 的 Enter → 觸發淡入（往 hover_opacity_
    // 過場）；本 surface 的 Leave → 觸發淡出（往 base_opacity_ 過場）。其餘（其他 surface /
    // Move）忽略。回傳訂閱代號（供呼叫端日後 `hover.unsubscribe(id)`；0 = 訂閱失敗）。
    ds::events::SubscriptionId attach(ds::events::HoverTracker& hover);

    // 注入式時間推進：把淡變經過時間推進 dt_seconds 秒（dt<0 視為 0），計算出推進後的目前
    // 不透明度並套用至綁定的 alpha surface（E1-03 `set_opacity`）。回傳套用結果：
    //   - Ok：已成功套用。
    //   - Unsupported：per-pixel alpha 能力不可用（NFR-03，`current_opacity()` 仍可查詢
    //     內部計算值，供呼叫端降級路徑使用）。
    //   - Invalid：surface 未於 E1-03 註冊為 alpha surface。
    WindowOpacityStatus advance(double dt_seconds);

    // 目前（動畫中）不透明度比例 [0,1]；純查詢，不改狀態、不重新套用至後端。
    float current_opacity() const noexcept { return current_opacity_; }

    // 目前是否處於「懸停中」狀態（依最近一次 attach 訂閱收到的 Enter / Leave 判定）。
    bool is_hovering() const noexcept { return hovering_; }

    // 目前設定的基準 / 懸停目標不透明度（未經動畫插值的目標值，非 current_opacity()）。
    float base_opacity() const noexcept { return base_opacity_; }
    float hover_opacity() const noexcept { return hover_opacity_; }

    // 本物件綁定的具名 surface（NFR-02：具名指涉，非數字 handle）。
    const SurfaceId& surface() const noexcept { return surface_; }

    // 設定的淡變歷時（秒）。
    double fade_seconds() const noexcept { return fade_seconds_; }

private:
    // 開始一段淡變：從目前不透明度往 target 過場，經過時間歸零。
    // fade_seconds_<=0 時視為瞬時：立即把 current_opacity_ 設為 target（無過場）。
    void start_fade(float target);
    // 把 opacity 套用至綁定的 alpha surface；轉發 E1-03 AlphaStatus → WindowOpacityStatus。
    WindowOpacityStatus apply(float opacity);
    // HoverTracker 訂閱回呼：僅回應本 surface 的 Enter / Leave。
    void on_hover_event(const ds::events::HoverEvent& event);

    AlphaSurfaceService& alpha_service_;
    SurfaceId surface_;
    double fade_seconds_;

    float base_opacity_ = 1.0f;   // 未懸停時的目標（設定值）
    float hover_opacity_ = 1.0f;  // 懸停中的目標（設定值）
    bool hovering_ = false;

    float current_opacity_ = 1.0f;  // 目前（動畫中）不透明度：advance() 推進之依據 / 結果
    float fade_from_ = 1.0f;
    float fade_to_ = 1.0f;
    double fade_elapsed_ = 0.0;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_15_WINDOW_OPACITY_HPP
