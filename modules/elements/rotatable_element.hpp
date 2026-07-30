// E4-19 旋轉元件 — 依角度旋轉顯示的元件（module 層 / 子系統 elements）
//
// 語意：把一個 E4-02 圖片元件依角度旋轉顯示——以 E4-22 2D 變形矩陣
// （`ds::render::Transform2D`）計算「繞具名旋轉中心(pivot)的旋轉」，以 E4-02
// `ImageElement` 顯示該圖片，產出**旋轉後渲染描述**（底層圖片渲染描述 + 旋轉變形矩陣）
// 供後續相位的繪製層消費。
//
// 相位 1 不做真實旋轉繪製：本單元只計算「應套用的變形矩陣」，不觸碰任何 OS / 繪圖 API，
// 無 `#ifdef` / win32 / cocoa。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - 角度以**弳度**（radians）表達——這是元件本身的旋轉語意輸出，非螢幕座標。
//   - 旋轉中心(pivot)以**具名列舉** `RotationPivot`（center/top-left/... 九宮格錨點，
//     沿用 E2-27 `ScreenAnchor` 的具名錨點精神）表達，非螢幕像素座標；內部依所綁 E4-02
//     來源的固有尺寸換算出「換算用」的內部支點，只作旋轉矩陣計算之用，不對外暴露任何
//     (x,y) 數字欄位。
//   - 目標 / 縮放 / 裁切 / 透明度沿用 E4-02 既有的相對佈局表達（具名 SurfaceId / 正規化
//     裁切 / 具名縮放模式），本單元未新增任何欄位破壞其精神。
//   - 未新增任何數字疊放層級(z-order)欄位。
//
// 連續旋轉：`set_angular_velocity` + `advance(dt)` 支援隨時間持續累加角度（可跨越多整
// 圈），內部以 2π 為模正規化角度，避免長時間運行浮點誤差無界累積；亦可 `rotate_by(delta)`
// 一次性疊加角度變化量。
//
// 錯誤不靜默：非有限角度（NaN/Inf）、非有限角速度 → 一律回 `RotateStatus::Invalid`，
// **不**套用、**不**靜默改值（承 E4-02 對非法輸入不靜默的精神）。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_19_ROTATABLE_ELEMENT_HPP
#define DS_ELEMENTS_E4_19_ROTATABLE_ELEMENT_HPP

#include <string>

#include "image_element.hpp"  // E4-02（上游，可讀不可改）：ImageElement / ImageSource / ImageRenderModel 等
#include "transform2d.hpp"    // E4-22（上游，可讀不可改）：ds::render::Transform2D / Vec2

namespace ds::elements {

// 操作結果碼 —— 與同子系統各 module 層單元同精神：明確、不靜默。
enum class RotateStatus {
    Ok,       // 操作成功
    Invalid,  // 前置條件違反（非有限角度 / 非有限角速度等）；不套用
};

// 旋轉中心（具名九宮格錨點；NFR-02：具名，非螢幕座標）。以所綁圖片來源固有尺寸的**比例
// 位置**換算：Center = 尺寸的 (0.5,0.5) 處、TopLeft = (0,0) 處……依此類推，供旋轉矩陣計算
// 內部支點使用，不對外暴露任何座標數字。
enum class RotationPivot {
    Center,
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

// 旋轉後渲染描述 —— 供後續相位的繪製層消費。
struct RotatedRenderModel {
    ImageRenderModel image;              // 底層 E4-02 圖片渲染描述（來源/縮放/裁切/透明度/目標）
    ds::render::Transform2D transform;   // 旋轉變形矩陣：繞 pivot 旋轉 angle_radians（E4-22）
    float angle_radians = 0.0f;          // 目前角度（弳度，正規化至 [0, 2π)）
    RotationPivot pivot = RotationPivot::Center;  // 目前旋轉中心
};

// ---------------------------------------------------------------------------
// RotatableElement —— 可旋轉元件：把一個 E4-02 圖片元件依角度旋轉顯示。
// 純邏輯、平台中立、不做真實旋轉繪製。
// ---------------------------------------------------------------------------
class RotatableElement {
public:
    RotatableElement() = default;

    // --- 角度 ---
    // 設定目前角度（弳度）。非有限值（NaN/Inf）→ `Invalid`（不套用，維持既有角度）。
    // 成功套用的值會正規化至 [0, 2π)（浮點穩定；2π 為一整圈，不影響旋轉語意）。
    RotateStatus set_angle(float radians);
    float angle() const noexcept { return angle_; }

    // 疊加角度變化量（一次性）：angle = angle + delta（正規化）。非有限 delta → `Invalid`
    // （不套用，維持既有角度）。
    RotateStatus rotate_by(float delta_radians);

    // --- 連續旋轉：角速度 + advance ---
    // 設定角速度（弳度 / 每單位時間）。非有限值 → `Invalid`（不套用，維持既有角速度）。
    RotateStatus set_angular_velocity(float radians_per_unit);
    float angular_velocity() const noexcept { return angular_velocity_; }

    // 以外部注入的時間增量 dt 推進角度：angle += angular_velocity * dt（正規化）。支援
    // 跨越多整圈的連續旋轉（如持續轉動的圖示）。角速度為 0、或計算結果非有限（如 dt 為
    // NaN/Inf）→ 安全 no-op（不崩潰、不把非有限值寫入角度）。
    void advance(double dt) noexcept;

    // --- 旋轉中心（具名） ---
    // 設定旋轉中心（具名九宮格錨點；列舉恆合法，恆回 `Ok`）。
    RotateStatus set_pivot(RotationPivot pivot) noexcept;
    RotationPivot pivot() const noexcept { return pivot_; }

    // --- 透傳 E4-02 顯示設定 ---
    ImageStatus set_source(const ImageSource& source) { return image_.set_source(source); }
    void clear_source() noexcept { image_.clear_source(); }
    bool has_source() const noexcept { return image_.has_source(); }
    const std::string& source_reference() const noexcept { return image_.source_reference(); }
    ImageDimensions source_dimensions() const noexcept { return image_.source_dimensions(); }
    ImageStatus set_scale_mode(ScaleMode mode) noexcept { return image_.set_scale_mode(mode); }
    ScaleMode scale_mode() const noexcept { return image_.scale_mode(); }
    ImageStatus set_opacity(float opacity) { return image_.set_opacity(opacity); }
    float opacity() const noexcept { return image_.opacity(); }
    ImageStatus set_target(const ds::kernel::SurfaceId& target) {
        return image_.set_target(target);
    }
    const ds::kernel::SurfaceId& target() const noexcept { return image_.target(); }

    // --- 旋轉變形矩陣 ---
    // 依目前角度 + pivot（換算自所綁來源固有尺寸）計算旋轉矩陣：
    //   translate(pivot) ∘ rotate(angle) ∘ translate(-pivot)
    // 即「先移到以 pivot 為原點，再旋轉，再移回」——標準繞任意點旋轉公式（E4-22 `compose`）。
    // 未綁來源（固有尺寸為零）時 pivot 換算為 (0,0)，等同繞原點旋轉（不報錯，明確行為）。
    ds::render::Transform2D transform() const;

    // --- 渲染描述 ---
    // 產出旋轉後渲染描述（底層 E4-02 渲染描述 + 旋轉變形矩陣 + 角度 + pivot）。
    RotatedRenderModel render_model() const;

private:
    ImageElement image_;                            // E4-02：顯示旋轉對象
    float angle_ = 0.0f;                            // 目前角度（弳度，正規化至 [0, 2π)）
    float angular_velocity_ = 0.0f;                 // 角速度（弳度 / 單位時間）
    RotationPivot pivot_ = RotationPivot::Center;   // 旋轉中心
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_19_ROTATABLE_ELEMENT_HPP
