// content/widgets/c2_06/animation_widget.hpp — C2-06 動畫 / 序列圖 widget（artifact 層 / 相位 1）
//
// 語意：桌面小工具「動畫 / 序列圖」——播放一組幀序列動畫（如 GIF / 雪碧圖），可宣告式設定幀來源
// / fps / 循環，支援播放 / 暫停 / 注入式 tick 推進、查詢目前幀，並產出可繪製的渲染描述。
// 這是一個**組裝型**單元（非新引擎邏輯）：把已合併的 **E4-07**（`ds::elements::FrameAnimationElement`）
// 幀序列動畫元件掛載到 **C1-01**（`ds::profiles::SkinProfile`）基底之上——widget 的渲染輸出
// 綁定到所掛載 skin 的具名 surface（`SkinProfile::id()`），供其宿主桌面角色顯示。
//
// 組裝的兩個擴充點：
//   - C1-01（`ds::profiles::SkinProfile`）：widget 掛載的基底（注入式相依，不取得其所有權，
//     須比本物件活得久）。本 widget 不驅動其載入 / 拖曳等生命週期（呼叫端自行管理），僅取用
//     其具名 id 作為渲染輸出的目標 surface（NFR-02：具名，非數字 handle）。
//   - E4-07（`ds::elements::FrameAnimationElement`）：幀序列動畫本體——本 widget 內部自持一個
//     實例，`configure()` 透過宣告式 `ds::format::Value`（E7-01）設定其幀清單 / fps / 循環，
//     `play()`/`pause()`/`advance()`/`current_frame()`/`render_model()` 皆為其薄封裝。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` / win32 /
// cocoa）、無絕對座標 / 數字 z-order（NFR-02）。時間全為**注入式 tick**（承 E4-07），不碰真實
// 時鐘 / 計時器 / 繪製後端。任何無效設定（宣告式定義結構 / 型別 / 具名值不合法）一律回傳明確
// `AnimationWidgetStatus::Invalid`，且**不**部分套用既有設定（全有或全無，承 E4-07 `set_frames`
// 慣例）。未設定幀序列（空幀）時所有查詢 / 播放操作皆安全降級（`render_model()` 回
// `has_source=false`，`advance`/`play`/`pause` 安全 no-op），不靜默假裝有資料。
//
// 命名空間 `ds::widgets`。
#ifndef DS_CONTENT_WIDGETS_C2_06_ANIMATION_WIDGET_HPP
#define DS_CONTENT_WIDGETS_C2_06_ANIMATION_WIDGET_HPP

#include <cstddef>

#include "document.hpp"                 // E7-01（上游，可讀不可改）：Value（宣告式設定的資料模型）
#include "frame_animation_element.hpp"  // E4-07（上游，可讀不可改）：FrameAnimationElement / Tick /
                                         //   ImageRenderModel 等
#include "skin_profile.hpp"             // C1-01（上游，可讀不可改）：SkinProfile（widget 掛載基底）

namespace ds::widgets {

// widget 設定操作的具名結果碼（NFR-02：具名，非數字）。
enum class AnimationWidgetStatus {
    Ok,       // 設定成功套用。
    Invalid,  // 宣告式定義結構 / 型別 / 具名值不合法；不套用（承 E4-07 精神，全有或全無）。
};

const char* to_string(AnimationWidgetStatus s) noexcept;

// ---------------------------------------------------------------------------
// AnimationWidget —— 動畫 / 序列圖 widget：組裝 C1-01（掛載基底）+ E4-07（幀序列動畫本體）。
//
// 每個實例掛載於**一個**注入的 C1-01 `SkinProfile`（渲染輸出綁定其具名 surface id）之上，
// 內部自持**一個** E4-07 `FrameAnimationElement`。宣告式設定（E7-01 `Value`）消費規則：
//
//   frames: List（必填，至少一項；每項為 Map）
//     - ref:    具名幀來源參照（字串，非空；NFR-02：具名，非數字 handle）
//       width:  幀固有寬（數字，> 0）
//       height: 幀固有高（數字，> 0）
//   fps:  選填，Number，> 0 且有限（未給沿用目前 fps，元件預設 1.0）
//   loop: 選填，Bool（未給沿用目前循環設定，元件預設 true）
//
// `definition` 非 Map、`frames` 缺失 / 非 List / 空清單、任一幀項結構或具名值不合法、`fps` /
// `loop` 型別或值不合法 —— 一律 `Invalid`，且**不**改變既有幀序列 / fps / 循環設定（不部分套用）。
// 未知鍵忽略。
// ---------------------------------------------------------------------------
class AnimationWidget {
public:
    // 掛載一個注入的 C1-01 基底。渲染輸出的目標 surface 立即綁定為 `skin.id()`
    // （NFR-02：具名目標，非數字 handle；`skin` 的 id 不可變，故僅需綁定一次）。
    explicit AnimationWidget(ds::profiles::SkinProfile& skin);

    AnimationWidget(const AnimationWidget&) = delete;
    AnimationWidget& operator=(const AnimationWidget&) = delete;

    // --- 宣告式設定（E7-01）---
    // 見類別註解的欄位規則。
    AnimationWidgetStatus configure(const ds::format::Value& definition);

    // --- 播放控制（透傳 E4-07；暫停中 / 空幀序列皆安全）---
    void play() noexcept;
    void pause() noexcept;
    bool is_playing() const noexcept;

    // 以邏輯時間增量 dt（tick，注入式）推進播放進度（透傳 E4-07 `advance`）。暫停中、空幀序列、
    // 或單次模式已播完 → 安全 no-op。
    void advance(ds::elements::Tick dt);

    // 將播放進度歸零，回到第 0 幀（不清空幀序列，不變更已設定的 fps / 循環 / 播放狀態）。
    void reset() noexcept;

    // --- 查詢 ---
    std::size_t current_frame() const noexcept;
    std::size_t frame_count() const noexcept;
    double fps() const noexcept;
    bool loop() const noexcept;
    bool is_finished() const noexcept;

    // 產出目前幀的渲染描述（透傳 E4-07 `render_model`）。空幀序列（未 `configure` 或設定被拒）
    // → `has_source=false` 的空渲染描述（明確降級，不靜默假裝有資料）。目標 surface 已綁定所掛
    // 載 C1-01 基底的具名 id。
    ds::elements::ImageRenderModel render_model() const;

    // 本 widget 所掛載的 C1-01 基底（唯讀存取，供驗證組裝正確）。
    const ds::profiles::SkinProfile& skin() const noexcept { return skin_; }

private:
    ds::profiles::SkinProfile& skin_;
    ds::elements::FrameAnimationElement animation_;
};

}  // namespace ds::widgets

#endif  // DS_CONTENT_WIDGETS_C2_06_ANIMATION_WIDGET_HPP
