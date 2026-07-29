// E4-25 背景模式 — 元件 / 面板的「背景呈現模式」渲染描述模型（module 層 / 子系統 elements）
//
// 語意：背景呈現模式——描述一個元件 / 面板的背景要怎麼畫：純色 / 漸層(可選) / 圖片(重用
// E4-02 `ImageElement`) / 模糊 / 透明，並附帶圓角、邊框，產出一份**背景渲染描述模型**
// （`BackgroundRenderModel`）供後續相位的繪製層消費。
//
// 相位 1**不做真實繪圖**：本單元不觸碰任何 OS / 繪圖 API、不連結任何影像 / 模糊函式庫、
// 無 `#ifdef` / `win32` / `cocoa`。圖片背景直接重用上游 E4-02 `ImageElement`（其本身已以
// 可注入 `ImageSource` 解耦真實解碼），本單元不重造圖片邏輯；模糊只表達為一個「模糊半徑」
// 描述值，交由後續相位的繪製層決定如何實作（高斯 / 其他）。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - **目標**以**具名 SurfaceId** 指涉（沿用上游 E1-03 / E4-02 慣例），不用數字 handle。
//   - **漸層方向**為**具名列舉**（Vertical/Horizontal/Diagonal），非角度數字。
//   - **漸層停駐點位置**為**正規化比例** [0,1]（沿漸層軸的分數），非螢幕像素座標。
//   - **顏色分量** / **不透明度**為 [0,1] 正規化比例（同 E1-03 `AlphaProfile.opacity` 精神）。
//   - **圓角半徑** / **邊框寬度**為描述背景「自身外觀」的非負尺寸值（如 E4-02 `ImageDimensions`
//     之於影像自身），非螢幕擺放位置；本單元完全不提供任何疊放層級 / z-order 欄位。
//
// 不靜默失敗：未知 / 越界的 `BackgroundMode`、`GradientDirection`、非有限或越界的漸層停駐點
// 位置、非有限顏色分量、非有限或負值的模糊半徑 / 圓角半徑 / 邊框寬度、非有限不透明度、空目標名
// 一律回 `BackgroundStatus::Invalid`，**不**套用、**不**靜默改值、**不**部分套用（既有合法狀態
// 不被破壞）。圖片背景的無效來源直接沿用 E4-02 `ImageElement::set_source` 的報錯語意。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_25_BACKGROUND_ELEMENT_HPP
#define DS_ELEMENTS_E4_25_BACKGROUND_ELEMENT_HPP

#include <string>
#include <vector>

#include "alpha_surface.hpp"  // E1-03（上游，可讀不可改）：AlphaProfile；並透過其 include 傳遞
                              // ds::kernel::SurfaceId（具名 surface）。
#include "image_element.hpp"  // E4-02（上游，可讀不可改）：圖片背景重用 ImageElement / ImageSource
                               // / ImageRenderModel，本單元不重造圖片解碼 / 縮放 / 裁切邏輯。

namespace ds::elements {

// 操作結果碼 —— 與同子系統各 module 層單元同精神：明確、不靜默。
enum class BackgroundStatus {
    Ok,       // 操作成功
    Invalid,  // 前置條件違反（未知模式、越界 / 非有限參數、空目標等）；不套用
};

// 背景呈現模式（具名列舉，非數字係數；NFR-02）。
enum class BackgroundMode {
    Solid,        // 純色填滿
    Gradient,     // 漸層（可選：由停駐點 + 方向描述）
    Image,        // 圖片（重用 E4-02 ImageElement）
    Blur,         // 模糊（描述值，真實模糊演算法交由後續相位繪製層）
    Transparent,  // 透明（不畫背景）
};

// 漸層方向（具名，非角度數字；NFR-02）。
enum class GradientDirection {
    Vertical,    // 由上到下
    Horizontal,  // 由左到右
    Diagonal,    // 對角線
};

// 該 `BackgroundMode` 值是否合法（防止未定義列舉值靜默流入渲染描述）。
inline bool is_valid_background_mode(BackgroundMode mode) noexcept {
    return mode == BackgroundMode::Solid || mode == BackgroundMode::Gradient ||
           mode == BackgroundMode::Image || mode == BackgroundMode::Blur ||
           mode == BackgroundMode::Transparent;
}

// 該 `GradientDirection` 值是否合法。
inline bool is_valid_gradient_direction(GradientDirection dir) noexcept {
    return dir == GradientDirection::Vertical || dir == GradientDirection::Horizontal ||
           dir == GradientDirection::Diagonal;
}

// 顏色 —— RGBA 四分量，各為 [0,1] 正規化比例（非螢幕座標；NFR-02）。
// 預設不透明黑（0,0,0,1）。
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

// 漸層停駐點 —— `position` 為沿漸層軸的**正規化比例** [0,1]（NFR-02：比例，非像素座標），
// `color` 為該點的顏色。
struct GradientStop {
    double position = 0.0;
    Color color;
};

// 邊框樣式 —— `width` 為非負的邊框寬度描述值（背景自身外觀尺寸，非螢幕位置）；
// 預設 `width == 0.0` 代表「無邊框」。
struct BorderStyle {
    Color color;
    double width = 0.0;
};

// 背景元件的**渲染描述模型** —— 供後續相位的繪製層消費。相位 1 不含任何真實繪製 / 模糊
// 演算法 / 影像解碼。
struct BackgroundRenderModel {
    BackgroundMode mode = BackgroundMode::Solid;
    Color color;                              // Solid 模式使用；其餘模式忽略但仍填出目前值。
    std::vector<GradientStop> gradient_stops;  // Gradient 模式使用；可為空（尚未設停駐點）。
    GradientDirection gradient_direction = GradientDirection::Vertical;
    ImageRenderModel image;                    // Image 模式使用（沿用 E4-02）；未設來源時
                                                // `image.has_source == false`。
    double blur_radius = 0.0;                  // Blur 模式使用；非負。
    double corner_radius = 0.0;                // 圓角半徑；非負；適用所有模式。
    BorderStyle border;                        // 邊框；width==0 表示無邊框；適用所有模式。
    ds::kernel::AlphaProfile alpha;            // 整體透明度（opacity 比例 [0,1]）。
    ds::kernel::SurfaceId target;              // 具名目標 surface（空 = 未綁定；NFR-02：具名）。
};

// ---------------------------------------------------------------------------
// BackgroundElement —— 背景呈現模式元件：選模式、設色 / 漸層 / 圖片 / 模糊、圓角、邊框、
// 透明度、目標，產出渲染描述模型。純邏輯、平台中立、無真實繪製 / 模糊 / 影像解碼。
// ---------------------------------------------------------------------------
class BackgroundElement {
public:
    BackgroundElement() = default;

    // --- 模式 ---
    // 設定背景呈現模式。未知 / 越界列舉值 → `Invalid`（不套用，模式維持原值）。
    BackgroundStatus set_mode(BackgroundMode mode) noexcept;
    BackgroundMode mode() const noexcept { return mode_; }

    // --- 純色 ---
    // 設定純色（Solid 模式使用）。任一分量非有限值 → `Invalid`（不套用）；成功 → `Ok`
    // （分量自動 clamp 至 [0,1]，同 E1-03 opacity 精神）。
    BackgroundStatus set_color(const Color& color) noexcept;
    const Color& color() const noexcept { return color_; }

    // --- 漸層（可選）---
    // 新增一個漸層停駐點。`position` 非有限值或超出 [0,1]、或 `color` 任一分量非有限值
    // → `Invalid`（不新增）；成功 → `Ok`（依呼叫順序附加，顏色分量 clamp 至 [0,1]）。
    BackgroundStatus add_gradient_stop(const GradientStop& stop);
    // 清空所有漸層停駐點。恆成功。
    void clear_gradient_stops() noexcept { gradient_stops_.clear(); }
    const std::vector<GradientStop>& gradient_stops() const noexcept { return gradient_stops_; }

    // 設定漸層方向。未知 / 越界列舉值 → `Invalid`（不套用）。
    BackgroundStatus set_gradient_direction(GradientDirection direction) noexcept;
    GradientDirection gradient_direction() const noexcept { return gradient_direction_; }

    // --- 圖片（重用 E4-02）---
    // 載入圖片背景來源；直接委派給內部 `ImageElement::set_source`（見 E4-02 語意：無效來源 /
    // 尺寸為零 → `Invalid`，不部分套用）。
    BackgroundStatus set_image(const ImageSource& source);
    // 卸載圖片背景來源（回到無來源狀態）。恆成功。
    void clear_image() noexcept { image_.clear_source(); }
    // 唯讀存取內部圖片元件（查詢用）。
    const ImageElement& image() const noexcept { return image_; }
    // 可變存取內部圖片元件 —— 供進一步設定縮放模式 / 裁切 / 目標等 E4-02 既有能力，
    // 本單元不重複這些設定介面。
    ImageElement& image() noexcept { return image_; }

    // --- 模糊 ---
    // 設定模糊半徑（描述值，非負）。非有限值或 < 0 → `Invalid`（不套用）。
    BackgroundStatus set_blur_radius(double radius) noexcept;
    double blur_radius() const noexcept { return blur_radius_; }

    // --- 透明度（沿用上游 AlphaProfile.opacity）---
    // 設定整體不透明度 [0,1]。非有限值 → `Invalid`（不套用）；成功 → `Ok`（自動 clamp）。
    BackgroundStatus set_opacity(float opacity) noexcept;
    float opacity() const noexcept { return alpha_.opacity; }
    const ds::kernel::AlphaProfile& alpha() const noexcept { return alpha_; }

    // --- 圓角 ---
    // 設定圓角半徑（非負）。非有限值或 < 0 → `Invalid`（不套用）。
    BackgroundStatus set_corner_radius(double radius) noexcept;
    double corner_radius() const noexcept { return corner_radius_; }

    // --- 邊框 ---
    // 設定邊框樣式。`width` 非有限值或 < 0 → `Invalid`（不套用）；顏色分量非有限值 →
    // `Invalid`；成功 → `Ok`（顏色分量 clamp 至 [0,1]）。
    BackgroundStatus set_border(const BorderStyle& border) noexcept;
    // 清除邊框（回到 `width == 0` 的無邊框狀態）。恆成功。
    void clear_border() noexcept { border_ = BorderStyle{}; }
    const BorderStyle& border() const noexcept { return border_; }

    // --- 目標具名 surface ---
    // 設定渲染目標（具名 SurfaceId）。空字串 → `Invalid`（不套用）。
    BackgroundStatus set_target(const ds::kernel::SurfaceId& target);
    // 解除目標綁定（回到未綁定的空目標）。恆成功。
    void clear_target() noexcept { target_.clear(); }
    const ds::kernel::SurfaceId& target() const noexcept { return target_; }

    // --- 渲染描述 ---
    // 產出目前狀態的渲染描述模型（模式 + 純色 + 漸層 + 圖片 + 模糊半徑 + 圓角 + 邊框 +
    // 透明度 + 目標）。
    BackgroundRenderModel render_model() const;

private:
    BackgroundMode mode_ = BackgroundMode::Solid;
    Color color_{};
    std::vector<GradientStop> gradient_stops_;
    GradientDirection gradient_direction_ = GradientDirection::Vertical;
    ImageElement image_;
    double blur_radius_ = 0.0;
    double corner_radius_ = 0.0;
    BorderStyle border_{};
    ds::kernel::AlphaProfile alpha_{};  // 預設 opacity = 1.0（完全不透明）。
    ds::kernel::SurfaceId target_;      // 空 = 未綁定。
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_25_BACKGROUND_ELEMENT_HPP
