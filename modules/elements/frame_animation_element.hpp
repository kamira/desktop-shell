// E4-07 幀序列動畫 — 幀序列(sprite/frame sequence)動畫元件（module 層 / 子系統 elements）
//
// 語意：一組圖片幀依序播放（如 GIF / 雪碧圖動畫）——以上游 **E4-09**
// （`ds::render::AnimationDriver`）動畫驅動推進「目前應顯示第幾幀」，以上游 **E4-02**
// （`ds::elements::ImageElement`）顯示目前幀，產出當前幀的**圖片渲染描述**
// （`ds::elements::ImageRenderModel`）。支援 fps（每 tick 前進幾幀，可小數）、循環 / 單次播放、
// 播放 / 暫停、查詢目前幀索引。
//
// 相位 1：時間全為**注入式** tick（沿用 E4-09/E5-04 的 `Tick = std::uint64_t`），不碰真實時鐘 /
// 計時器 / 繪製後端；平台中立、無 `#ifdef` / win32 / cocoa。推進來源可為外部手動注入的
// `advance(dt)`，也可 `attach()` 一個上游 E4-09 `AnimationDriver`，由其心跳脈衝自動呼叫。
//
// **NFR-02 鐵律**：本單元不新增任何絕對座標或數字 z-order —— `render_model()` 直接回傳 E4-02
// 的渲染描述（正規化裁切 [0,1]、具名 `SurfaceId` 目標、具名縮放模式列舉），未新增任何螢幕座標 /
// 數字疊放層級欄位。
//
// 錯誤不靜默：空幀序列、任一幀來源無效或尺寸非正、非正 / 非有限 fps → 一律回
// `FrameAnimationStatus::Invalid`，**不**套用、**不**靜默改值（承 E4-02 對非法輸入不靜默的精神）。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_07_FRAME_ANIMATION_ELEMENT_HPP
#define DS_ELEMENTS_E4_07_FRAME_ANIMATION_ELEMENT_HPP

#include <cstddef>
#include <functional>
#include <vector>

#include "animation_driver.hpp"  // E4-09（上游，可讀不可改）：AnimationDriver / AnimationId / Tick
#include "image_element.hpp"     // E4-02（上游，可讀不可改）：ImageElement / ImageSource / ImageRenderModel 等

namespace ds::elements {

// 邏輯時間單位，沿用 E4-09（= E5-04 的 Tick）。
using Tick = ds::render::Tick;

// 操作結果碼 —— 與同子系統各 module 層單元同精神：明確、不靜默。
enum class FrameAnimationStatus {
    Ok,       // 操作成功
    Invalid,  // 前置條件違反（空幀序列、無效幀來源、非正 / 非有限 fps 等）；不套用
};

// -----------------------------------------------------------------------------
// FrameAnimationElement —— 幀序列動畫元件。
//
// 持有一組圖片幀（依序播放，值語意複製自注入的 `ImageSource` —— 承 E4-02 `set_source`
// 慣例：只擷取參照字串 + 固有尺寸，不持有懸空參照，來源物件可於 `set_frames` 後即銷毀）與一個
// 「累計幀進度」（可為小數，取整數部分為目前幀索引）。以注入式 `advance(dt)` 依所設 fps
// （每 tick 前進幾幀）推進進度；`render_model()` 委由內部 E4-02 `ImageElement` 對「目前幀」
// 產出渲染描述。
// -----------------------------------------------------------------------------
class FrameAnimationElement {
public:
    FrameAnimationElement() = default;

    // 設定幀序列（依序播放的圖片幀，以 vector 順序即播放順序）。
    //   - `frames` 為空、或任一來源 `!valid()`、或其固有尺寸寬高任一 <= 0 → `Invalid`，
    //     且**不**改變既有幀序列與播放狀態（不部分套用，承 E4-02 `set_source` 慣例）。
    //   - 成功 → `Ok`：以值語意複製每幀的參照 + 固有尺寸；目前幀索引與累計進度歸零（從頭播放），
    //     播放中 / 暫停、循環設定、fps 皆不變。
    FrameAnimationStatus set_frames(const std::vector<std::reference_wrapper<const ImageSource>>& frames);

    // 現有幀數。
    std::size_t frame_count() const noexcept { return frames_.size(); }

    // fps：每 tick 前進幾幀（可小數；承 E4-11 `chars_per_tick` 慣例的命名精神——呼叫端自行
    // 決定一個 tick 代表多少真實時間，本單元只關心「每 tick 前進的幀數速率」）。
    // 非正值（<= 0）或非有限值（NaN / inf） → `Invalid`（不套用）。
    FrameAnimationStatus set_fps(double fps);
    double fps() const noexcept { return fps_; }

    // 循環播放（預設 true）。單次模式（false）播放到最後一幀即停在最後一幀並標記完成
    // （見 `is_finished()`），不再前進；循環模式恆不完成（除非幀序列為空）。
    void set_loop(bool loop) noexcept { loop_ = loop; }
    bool loop() const noexcept { return loop_; }

    // 播放 / 暫停：暫停中呼叫 `advance` 為安全 no-op（進度不動）。
    void play() noexcept { playing_ = true; }
    void pause() noexcept { playing_ = false; }
    bool is_playing() const noexcept { return playing_; }

    // 以邏輯時間增量 dt（tick）推進播放進度：累計進度 += fps() * dt。
    //   - 暫停中、空幀序列、或單次模式已播完 → 安全 no-op。
    //   - 循環模式：進度以幀數為模（wrap），恆在 `[0, frame_count())` 內循環。
    //   - 單次模式：進度夾在最後一幀索引，抵達即標記完成（`is_finished()` 轉為 true）。
    void advance(Tick dt);

    // 綁一個 E4-09 動畫驅動源：其每次脈衝（未暫停時）以該脈衝的 dt 自動呼叫 advance()。
    // 回傳所綁動畫的 AnimationId（供之後對該驅動源 remove / pause / resume 使用）；
    // driver 的生命週期須涵蓋本物件（或至遲於 remove 前解除綁定）。
    ds::render::AnimationId attach(ds::render::AnimationDriver& driver);

    // 目前幀索引（0-based）。空幀序列恆回 0。
    std::size_t current_frame() const noexcept { return index_; }

    // 是否已播完：空幀序列恆 true（無可播放內容）；循環模式恆 false；單次模式抵達最後一幀後為
    // true。
    bool is_finished() const noexcept;

    // 將播放進度歸零，回到第 0 幀（不清空幀序列，不變更已設定的 fps / 循環 / 播放狀態）。
    void reset() noexcept;

    // --- 透傳 E4-02 顯示設定，套用於「目前幀」的渲染描述 ---
    ImageStatus set_scale_mode(ScaleMode mode) noexcept { return image_.set_scale_mode(mode); }
    ScaleMode scale_mode() const noexcept { return image_.scale_mode(); }
    ImageStatus set_opacity(float opacity) { return image_.set_opacity(opacity); }
    float opacity() const noexcept { return image_.opacity(); }
    ImageStatus set_target(const ds::kernel::SurfaceId& target) { return image_.set_target(target); }
    const ds::kernel::SurfaceId& target() const noexcept { return image_.target(); }

    // 產出目前幀的渲染描述（委由 E4-02 `ImageElement::render_model()`）。空幀序列 →
    // `has_source=false` 的空渲染描述（明確，不靜默假裝有資料）。
    ImageRenderModel render_model() const { return image_.render_model(); }

private:
    // 把 frames_[index_] 同步設進 image_（E4-02）；空幀序列則清空來源。
    void sync_current_source();

    std::vector<MemoryImageSource> frames_;  // 依序播放的幀（值語意複製自注入來源）
    ImageElement image_;                     // E4-02：顯示目前幀
    std::size_t index_ = 0;                  // 目前幀索引
    double progress_ = 0.0;                  // 累計幀進度（含小數殘餘）
    double fps_ = 1.0;                       // 每 tick 前進幾幀
    bool loop_ = true;                       // 循環播放
    bool playing_ = false;                   // 播放中（預設暫停，需顯式 play()）
    bool finished_ = false;                  // 單次模式播完旗標
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_07_FRAME_ANIMATION_ELEMENT_HPP
