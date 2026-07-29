// E4-04 按鈕三態 — 依互動狀態(常態/懸停/按下)切換外觀的按鈕元件（module 層 / 子系統 elements）
//
// 語意：按鈕元件依互動狀態切換視覺——以上游 E5-02 `HoverTracker` 的懸停進出事件切換
// hover 態、以上游 E1-04 `HitTester`（命中）+ 按下/放開動作切換 pressed 態、各態顯示對應
// 上游 E4-02 `ImageElement` 圖片/樣式，產出**當前態渲染描述**（含目前 `ButtonState` +
// 該態的 `ImageRenderModel`）供後續相位的繪製層消費，並於「按下後於本按鈕內放開」時觸發
// `on_click` 回呼。
//
// 三態優先序（顯示用，`state()` 推導）：Pressed 優先於 Hover，Hover 優先於 Normal——
// 即按住時即使仍在懸停也顯示 Pressed；放開後若仍懸停則回到 Hover，否則回到 Normal。
// `hover_` 與 `pressed_` 為**各自獨立**追蹤的旗標（懸停狀態不因按下而被清除），避免「放開
// 時遺失懸停資訊」的錯誤重建。
//
// 相位 1 硬約束：純邏輯、平台中立，無真實視窗系統 / OS / 繪圖 API、無滑鼠鉤子；懸停移動與
// 按下/放開動作皆由呼叫端 / 測試**注入**（沿用 E5-02 `inject_move` / 本單元 `press`/`release`），
// 無 `#ifdef` / `win32` / `cocoa`。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - 座標沿用 E1-04 `LocalPoint`（元件本地 / 相對座標，非螢幕絕對座標）。
//   - 按鈕自身以**具名 SurfaceId** 指涉（非數字 handle），圖層以 E1-04 具名 `SurfaceLayer`
//     表達（非數字 z-order）。
//   - 各態視覺沿用 E4-02 `ImageRenderModel`（具名目標 / 正規化裁切 / 比例透明度），本單元
//     不新增任何 (x,y) 螢幕位置或疊放層級欄位。
//
// 不靜默失敗：
//   - 建構子空 `SurfaceId` → 擲 `std::invalid_argument`（不可指涉的按鈕沒有意義）。
//   - `set_visual` 給超出 `ButtonState` 三個列舉值範圍的無效態（如由整數 static_cast 而來）
//     → 回 `ButtonStatus::Invalid`，不套用。
//   - `press`/`release` 給非有限座標、或按鈕自身形狀無效（沿用 E1-04 `HitTester::is_valid`
//     判定）→ 回 `ButtonStatus::Invalid`，不改變按下狀態、不觸發 `on_click`。
//   - 懸停事件透過 E5-02 `HoverTracker` 分派；hit-test 無效時 E5-02 本身已回報不靜默
//     （`inject_move` 回 false），本單元的 `handle_hover_event` 僅單純轉譯已分派之事件。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_04_BUTTON_ELEMENT_HPP
#define DS_ELEMENTS_E4_04_BUTTON_ELEMENT_HPP

#include <array>
#include <functional>

#include "hover_tracker.hpp"  // E5-02（上游，可讀不可改）：HoverTracker / HoverEvent / SubscriptionId；
                              // 並透過其 include 傳遞 E1-04 LocalPoint / Shape / HitTester /
                              // HitSurface / SurfaceLayer / HitPolicy / SurfaceId。
#include "image_element.hpp"  // E4-02（上游，可讀不可改）：ImageElement / ImageRenderModel

namespace ds::elements {

// 操作結果碼 —— 與同子系統各 module 層單元同精神：明確、不靜默。
enum class ButtonStatus {
    Ok,       // 操作成功
    Invalid,  // 前置條件違反（無效態、非有限座標、按鈕形狀無效等）；不套用
};

// 按鈕三態（具名，非數字）。顯示優先序：Pressed > Hover > Normal（見 `state()`）。
enum class ButtonState {
    Normal,   // 常態：既未懸停亦未按下
    Hover,    // 懸停：滑鼠懸停於按鈕上、且未按下
    Pressed,  // 按下：按下動作命中按鈕後、放開前（不論此刻是否仍懸停，顯示皆優先為 Pressed）
};

// 按鈕的完整渲染描述 —— 純資料（NFR-02：無絕對座標 / 無數字 z-order）。
struct ButtonRenderModel {
    ButtonState state = ButtonState::Normal;  // 目前顯示態
    ImageRenderModel visual;                  // 對應該態的圖片渲染描述（E4-02）；未設定則為預設空描述
};

// 點擊回呼 —— 於「按下後於本按鈕範圍內放開」時觸發一次，無參數（如需事件資料，呼叫端可於
// lambda 捕獲上下文）。
using ClickCallback = std::function<void()>;

// ---------------------------------------------------------------------------
// ButtonElement —— 按鈕三態元件：整合 E4-02（各態視覺）+ E1-04（命中）+ E5-02（懸停事件），
// 依互動狀態切換外觀，產出渲染描述與點擊回呼。純邏輯、平台中立。
// ---------------------------------------------------------------------------
class ButtonElement {
public:
    // 建構一個具名按鈕：`id` 為本按鈕的具名 SurfaceId（供命中優先 / 懸停追蹤辨識，NFR-02
    // 具名非數字），`shape` 為本地座標命中形狀（見 E1-04 `Shape` / `make_rect` 等工廠），
    // `layer` 為具名命中優先圖層（預設 `Normal`）。
    //   - `id` 為空字串 → 擲 `std::invalid_argument`（不可指涉的按鈕沒有意義，不靜默接受）。
    ButtonElement(ds::kernel::SurfaceId id, ds::kernel::Shape shape,
                  ds::kernel::SurfaceLayer layer = ds::kernel::SurfaceLayer::Normal);

    const ds::kernel::SurfaceId& id() const noexcept { return id_; }
    const ds::kernel::Shape& shape() const noexcept { return shape_; }
    ds::kernel::SurfaceLayer layer() const noexcept { return layer_; }

    // --- 各態視覺（E4-02）---
    // 設定某態要顯示的圖片元件（**值複製**：呼叫後本按鈕持有獨立副本，`image` 可安全銷毀 /
    // 之後再變動不影響已設定的視覺）。`state` 超出三個合法列舉值 → `Invalid`，不套用。
    ButtonStatus set_visual(ButtonState state, const ImageElement& image);
    // 查詢某態目前設定的圖片元件；`state` 非法 → 回傳 `nullptr`。未曾 `set_visual` 過的態
    // 回傳指向預設（`has_source()==false`）`ImageElement` 的指標，非 `nullptr`。
    const ImageElement* visual(ButtonState state) const;

    // --- 點擊回呼 ---
    void set_on_click(ClickCallback callback) { on_click_ = std::move(callback); }

    // --- 懸停整合（E5-02）---
    // 向一個 `HoverTracker` 註冊本按鈕的命中形狀並訂閱懸停事件，轉譯為 hover 態切換。
    // 回傳訂閱代號，供之後 `detach` 或直接呼叫 `tracker.unsubscribe` 使用；本物件不管理
    // `tracker` 的生命週期（`tracker` 須存活於訂閱期間）。
    ds::events::SubscriptionId attach(ds::events::HoverTracker& tracker);
    // 便利函式：解除先前 `attach` 建立的訂閱並移除本按鈕於 `tracker` 的命中形狀登記。
    // 回傳兩者是否皆確實移除（皆成功 → true）。
    bool detach(ds::events::HoverTracker& tracker, ds::events::SubscriptionId subscription);
    // 直接處理一則懸停事件（供測試 / 手動驅動，無需透過真實 `HoverTracker`）。
    // 非本按鈕（`event.surface != id()`）的事件忽略（no-op）。Enter → hover 態開；
    // Leave → hover 態關；Move → 保持不變（不重發 Enter）。
    void handle_hover_event(const ds::events::HoverEvent& event) noexcept;

    // --- 按下 / 放開（E1-04 命中）---
    // 於本地座標 `point` 對本按鈕形狀做命中測試；命中則進入 Pressed（顯示優先於 Hover）。
    // 未命中則不改變按下狀態（單純無動作，非錯誤）。
    //   - 形狀無效或 `point` 非有限值 → `Invalid`，不改變任何狀態（報錯不靜默）。
    ButtonStatus press(const ds::kernel::LocalPoint& point);
    // 於本地座標 `point` 對本按鈕形狀做命中測試並結束按下狀態：
    //   - 若先前處於 Pressed 且本次命中（仍在按鈕範圍內放開）→ 觸發 `on_click`（若已設定）。
    //   - 若先前非 Pressed（無對應的 `press`），或本次未命中（拖出範圍外放開）→ 結束按下但
    //     不觸發 `on_click`；後者為正常「取消點擊」語意，非錯誤。
    //   - 形狀無效或 `point` 非有限值 → `Invalid`，不改變任何狀態、不觸發 `on_click`。
    ButtonStatus release(const ds::kernel::LocalPoint& point);

    // --- 狀態查詢 ---
    bool is_hovered() const noexcept { return hover_; }
    bool is_pressed() const noexcept { return pressed_; }
    // 目前顯示態：Pressed > Hover > Normal（見類別註解）。
    ButtonState state() const noexcept;

    // --- 渲染描述 ---
    // 產出目前狀態的渲染描述：`state()` + 對應態的 `ImageElement::render_model()`。
    ButtonRenderModel render_model() const;

private:
    ds::kernel::SurfaceId id_;
    ds::kernel::Shape shape_;
    ds::kernel::SurfaceLayer layer_;
    ds::kernel::HitTester tester_;  // 無狀態，供 press/release 對本按鈕形狀做命中測試

    std::array<ImageElement, 3> visuals_{};  // 索引依 ButtonState{Normal,Hover,Pressed} 對應
    ClickCallback on_click_;

    bool hover_ = false;
    bool pressed_ = false;
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_04_BUTTON_ELEMENT_HPP
