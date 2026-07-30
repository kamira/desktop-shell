// E4-11 逐字顯示 — 打字機效果（module 層 / 子系統 elements）
//
// 語意：把一段文字以「逐字漸次顯示」(typewriter / typing effect) 呈現——外部以邏輯時間增量
// （tick / dt）推進「目前顯示到第 N 字」的進度，內部以上游 **E4-01**（`ds::render::TextLayout`）
// 對「目前已顯示的子字串」排版，產出渲染描述（`ds::render::LayoutResult`）。推進來源可為外部
// 手動注入的 `advance(dt)`，也可 `attach()` 一個上游 **E4-09**（`ds::render::AnimationDriver`）
// 動畫驅動源，由其心跳脈衝自動呼叫 `advance`。
//
// 相位 1：時間全為**注入式** tick（沿用 E4-09/E5-04 的 `Tick = std::uint64_t`），不碰真實時鐘 /
// 計時器 / 繪製後端；平台中立、無 `#ifdef` / win32 / cocoa。
//
// **NFR-02 鐵律**：本單元不新增任何絕對座標或數字 z-order —— `render_model()` 直接回傳
// E4-01 的相對佈局渲染描述（相對偏移 x/y、行內 advance；目標 surface 以具名 `SurfaceId` 指涉）。
//
// 錯誤不靜默：非法 UTF-8 文字、非正 / 非有限的速度 → 一律擲 `std::invalid_argument`（承 E4-01
// 對非法輸入不靜默的精神）。
#ifndef DS_ELEMENTS_E4_11_TYPEWRITER_ELEMENT_HPP
#define DS_ELEMENTS_E4_11_TYPEWRITER_ELEMENT_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "animation_driver.hpp"  // E4-09（上游，可讀不可改）：AnimationDriver / AnimationId / Tick
#include "text_layout.hpp"       // E4-01（上游，可讀不可改）：TextLayout / LayoutConstraints / LayoutResult

namespace ds::elements {

// 邏輯時間單位，沿用 E4-09（= E5-04 的 Tick）。
using Tick = ds::render::Tick;

// -----------------------------------------------------------------------------
// TypewriterElement —— 逐字顯示元件。
//
// 持有一段完整文字（UTF-8）與一個「目前顯示進度」（已顯示碼位數，可為小數累計、對外回報時
// 取整數部分）。以注入式 `advance(dt)` 依所設速度（每 tick 幾個字元）推進進度；`render_model()`
// 以上游 E4-01 對「目前已顯示的前綴子字串」排版，回傳相對佈局渲染描述。
//
// 不取得 FontMetrics 的所有權（承 E4-01 慣例）：其生命週期須涵蓋本物件。
// -----------------------------------------------------------------------------
class TypewriterElement {
public:
    // metrics：字型度量（不取得所有權）。constraints：排版約束（轉交 E4-01）。
    // surface：可選目標具名 surface（NFR-02，轉交 E4-01 渲染描述）。
    explicit TypewriterElement(const ds::render::FontMetrics& metrics,
                                ds::render::LayoutConstraints constraints = {},
                                ds::kernel::SurfaceId surface = {});

    // 設定完整顯示文字（UTF-8）。非法 UTF-8 序列 → std::invalid_argument（不靜默；沿用 E4-01
    // decode_utf8 的驗證）。設定新文字會將顯示進度歸零（重新從頭逐字顯示）。
    void set_text(const std::string& text);

    // 設定推進速度：每 tick 顯示幾個字元（可為小數，例如 0.5 = 每 2 tick 顯示 1 字元）。
    // 非正值（<= 0）或非有限值（NaN / inf） → std::invalid_argument（不靜默）。
    void set_speed(double chars_per_tick);

    double speed() const noexcept { return speed_; }

    // 以邏輯時間增量 dt（tick）推進顯示進度：累計進度 += speed() * dt，取整數部分為
    // 已顯示碼位數，並夾在 [0, total_count()] 內（不足 1 字元的殘餘進度予以保留累計，
    // 故速度 < 1 char/tick 時仍會在累計足夠 tick 後正確前進，不會卡住）。完成後（visible ==
    // total）再 advance 為安全 no-op（進度維持在總字數，不需呼叫端另行判斷 is_complete()）。
    void advance(Tick dt);

    // 綁一個 E4-09 動畫驅動源：其每次脈衝（未暫停時）以該脈衝的 dt 自動呼叫 advance()。
    // 回傳所綁動畫的 AnimationId（供之後對該驅動源 remove / pause / resume 使用）；
    // driver 的生命週期須涵蓋本物件（或至遲於 remove 前解除綁定）。
    ds::render::AnimationId attach(ds::render::AnimationDriver& driver);

    // 目前已顯示的碼位（字元）數。
    std::size_t visible_count() const noexcept;

    // 完整文字的碼位（字元）總數。
    std::size_t total_count() const noexcept { return codepoints_.size(); }

    // 是否已顯示完整段文字（含空文字 = 立即完成）。
    bool is_complete() const noexcept;

    // 將顯示進度歸零（不清空 / 不變更已設定的文字與速度），重新從頭逐字顯示。
    void reset() noexcept;

    // 產出「目前顯示到第 N 字」的渲染描述：對已顯示前綴子字串呼叫 E4-01 TextLayout::layout()。
    // 空文字或進度為 0 → 空渲染描述（沿用 E4-01：空字串排版得空結果）。
    ds::render::LayoutResult render_model() const;

private:
    // 依目前 codepoints_ 重建 UTF-8 位元組邊界表（長度 = total_count()+1；boundaries_[i] 為
    // 第 i 個碼位起始的位元組偏移，boundaries_[total_count()] = text_.size()）。
    void rebuild_boundaries();

    ds::render::TextLayout layout_;             // 轉交 E4-01 排版（含度量與 surface 綁定）
    ds::render::LayoutConstraints constraints_;  // 排版約束

    std::string text_;                        // 完整原始 UTF-8 文字
    std::vector<ds::render::CodePoint> codepoints_;  // 解碼後碼位序列
    std::vector<std::size_t> boundaries_;      // UTF-8 位元組邊界（長度 = codepoints_.size()+1）

    double speed_ = 1.0;     // 每 tick 顯示幾個字元
    double progress_ = 0.0;  // 累計顯示進度（含小數殘餘）
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_11_TYPEWRITER_ELEMENT_HPP
