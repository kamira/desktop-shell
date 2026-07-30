// content/widgets/c2_05/image_widget.hpp — C2-05 圖片 widget（artifact 層 / 相位 1）
//
// 語意：桌面小工具「圖片」——顯示單張，或依序切換一組圖片（如相框輪播），可宣告式設定圖片
// 來源清單 / 縮放（fit）模式，支援以索引直接切換或依序前進到下一張，並產出可繪製的渲染描述。
// 這是一個**組裝型**單元（非新引擎邏輯）：把已合併的 **E4-02**（`ds::elements::ImageElement`）
// 圖片元件掛載到 **C1-01**（`ds::profiles::SkinProfile`）基底之上——widget 的渲染輸出綁定到
// 所掛載 skin 的具名 surface（`SkinProfile::id()`），供其宿主桌面角色顯示。
//
// 組裝的兩個擴充點：
//   - C1-01（`ds::profiles::SkinProfile`）：widget 掛載的基底（注入式相依，不取得其所有權，
//     須比本物件活得久）。本 widget 不驅動其載入 / 拖曳等生命週期（呼叫端自行管理），僅取用
//     其具名 id 作為渲染輸出的目標 surface（NFR-02：具名，非數字 handle）。
//   - E4-02（`ds::elements::ImageElement`）：單張圖片本體——本 widget 內部自持一個實例，
//     `configure()` 透過宣告式 `ds::format::Value`（E7-01）設定一組圖片來源（清單）與縮放
//     （fit）模式；widget 層維護清單與目前索引，逐一把「目前所選的一張」載入 E4-02（透過
//     `set_source`），`set_image()`/`next()` 切換所選，`render_model()` 為其薄封裝。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` / win32 /
// cocoa）、無絕對座標 / 數字 z-order（NFR-02）、無真實影像解碼（沿用 E4-02 的可注入
// `ImageSource` 抽象，僅需固有尺寸與具名參照）。任何無效設定（宣告式定義結構 / 型別 / 具名值
// 不合法）一律回傳明確 `ImageWidgetStatus::Invalid`，且**不**部分套用既有設定（全有或全無）。
// 未設定圖片（空清單）時所有查詢 / 切換操作皆安全降級（`render_model()` 回 `has_source=false`，
// `next()` 安全 no-op，`set_image()` 回 `Invalid`），不靜默假裝有資料。
//
// 命名空間 `ds::widgets`。
#ifndef DS_CONTENT_WIDGETS_C2_05_IMAGE_WIDGET_HPP
#define DS_CONTENT_WIDGETS_C2_05_IMAGE_WIDGET_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "document.hpp"       // E7-01（上游，可讀不可改）：Value（宣告式設定的資料模型）
#include "image_element.hpp"  // E4-02（上游，可讀不可改）：ImageElement / ImageDimensions /
                               //   ScaleMode / ImageRenderModel / MemoryImageSource
#include "skin_profile.hpp"   // C1-01（上游，可讀不可改）：SkinProfile（widget 掛載基底）

namespace ds::widgets {

// widget 設定 / 切換操作的具名結果碼（NFR-02：具名，非數字）。
enum class ImageWidgetStatus {
    Ok,       // 操作成功套用。
    Invalid,  // 宣告式定義結構 / 型別 / 具名值不合法，或索引越界；不套用（全有或全無）。
};

const char* to_string(ImageWidgetStatus s) noexcept;

// ---------------------------------------------------------------------------
// ImageWidget —— 圖片 widget：組裝 C1-01（掛載基底）+ E4-02（單張圖片本體）。
//
// 每個實例掛載於**一個**注入的 C1-01 `SkinProfile`（渲染輸出綁定其具名 surface id）之上，
// 內部自持**一個** E4-02 `ImageElement`，並持有一份「圖片來源清單」（widget 層維護，E4-02
// 本身只認識單張）。`configure()` 消費的宣告式（E7-01 `Value`）規則：
//
//   images: List（必填，至少一項；每項為 Map）
//     - ref:    具名圖片來源參照（字串，非空；NFR-02：具名，非數字 handle）
//       width:  來源固有寬（數字，> 0）
//       height: 來源固有高（數字，> 0）
//   fit:  選填，具名縮放模式字串（"fill"/"fit"/"stretch"/"center"/"tile"；NFR-02：具名，非
//         數字係數）。未給沿用目前縮放模式（元件預設 `Fit`）。
//
// `definition` 非 Map、`images` 缺失 / 非 List / 空清單、任一圖片項結構或具名值不合法、`fit`
// 型別或具名值不合法 —— 一律 `Invalid`，且**不**改變既有圖片清單 / 目前索引 / 縮放模式（不
// 部分套用）。未知鍵忽略。設定成功後目前索引重置為第 0 張（承 E4-07 兄弟單元 `configure`
// 「重新設定即從頭開始」慣例）。
// ---------------------------------------------------------------------------
class ImageWidget {
public:
    // 掛載一個注入的 C1-01 基底。渲染輸出的目標 surface 立即綁定為 `skin.id()`
    // （NFR-02：具名目標，非數字 handle；`skin` 的 id 不可變，故僅需綁定一次）。
    explicit ImageWidget(ds::profiles::SkinProfile& skin);

    ImageWidget(const ImageWidget&) = delete;
    ImageWidget& operator=(const ImageWidget&) = delete;

    // --- 宣告式設定（E7-01）---
    // 見類別註解的欄位規則。
    ImageWidgetStatus configure(const ds::format::Value& definition);

    // --- 圖片切換 ---
    // 以索引直接選取一張已設定的圖片（載入其來源至 E4-02，其餘設定如 fit 模式不變）。
    // 尚未 configure（清單為空）或索引越界 → `Invalid`，且不改變目前所選。成功 → `Ok`。
    ImageWidgetStatus set_image(std::size_t index);

    // 前進到清單中的下一張（依序，抵達尾端後循環繞回第 0 張）。清單為空或僅一張 → 安全
    // no-op（不崩潰、不靜默改狀態）。
    void next();

    // --- 查詢 ---
    std::size_t image_count() const noexcept;
    std::size_t current_index() const noexcept;
    ds::elements::ScaleMode fit_mode() const noexcept;

    // 產出目前所選圖片的渲染描述（透傳 E4-02 `render_model`）。空清單（未 `configure` 或設定
    // 被拒）→ `has_source=false` 的空渲染描述（明確降級，不靜默假裝有資料）。目標 surface
    // 已綁定所掛載 C1-01 基底的具名 id。
    ds::elements::ImageRenderModel render_model() const;

    // 本 widget 所掛載的 C1-01 基底（唯讀存取，供驗證組裝正確）。
    const ds::profiles::SkinProfile& skin() const noexcept { return skin_; }

private:
    // 把目前索引所指的圖片來源載入 E4-02（清單為空時清除來源）。呼叫前提：清單非空時
    // `current_` 必為合法索引（由 configure/set_image/next 維護此不變量）。
    void apply_current();

    ds::profiles::SkinProfile& skin_;
    ds::elements::ImageElement image_;

    std::vector<std::string> refs_;
    std::vector<ds::elements::ImageDimensions> dims_;
    std::size_t current_ = 0;
};

}  // namespace ds::widgets

#endif  // DS_CONTENT_WIDGETS_C2_05_IMAGE_WIDGET_HPP
