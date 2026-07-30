// E4-28 透視與景深呈現 — 用 2D 變形模擬 2.5D 透視效果的「渲染描述模型」
// （module 層 / 子系統 elements）
//
// 語意：以上游 E4-22 2D 變形矩陣（`ds::render::Transform2D`）模擬 2.5D 透視(perspective)效果——
// 依**相對深度**換算縮放（越遠越小）、依具名消失點方向做視差(parallax)位移（越遠越朝消失點
// 方向靠攏），並可選附加景深模糊(depth of field)量。產出一份**帶透視變形的渲染描述**
// （變形矩陣 + 深度 + 等效縮放 + 景深模糊量）供後續相位的繪製層消費。
//
// 相位 1 不做真實繪製：本單元只計算「應套用的透視變形矩陣」與相關數值，不觸碰任何 OS / 繪圖
// API，無 `#ifdef` / win32 / cocoa。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - 深度(depth)是**相對值**——0 為基準面，正值表示更遠（縮小），負值表示更近（放大）；
//     不是螢幕像素座標，也不是疊放層級(z-order)索引。深度之間只有相對大小意義，無需對齊
//     任何畫面座標系。
//   - 消失點(vanishing point)以**具名九宮格方向** `VanishingPoint`（center/top/.../
//     bottom-right，沿用 E4-19 `RotationPivot` / E2-27 `ScreenAnchor` 的具名錨點精神）表達，
//     內部換算為單位方向向量供視差位移計算，不對外暴露任何 (x,y) 螢幕座標數字。
//   - 對外唯一的「幾何」輸出是 E4-22 `Transform2D`（矩陣），由呼叫端自行套用於實際 surface；
//     本單元不持有、不輸出任何絕對畫面座標。
//   - 未新增任何數字疊放層級(z-order)欄位——深度只驅動視覺縮放 / 位移 / 模糊，非繪製順序。
//
// 透視換算模型（相對、確定性）：
//   scale(d) = 1 / (1 + perspective_strength * d)
//   位於基準面 d=0 時 scale=1（不變）；d 越大（越遠）scale 越小；d 為負（越近）scale 越大。
//   當 (1 + perspective_strength * d) 趨近或跨越 0（深度已達或超越與消失點重合的臨界點，
//   物理上等同「跑到鏡頭後面 / 無限放大」的退化情形）——**明確回報 `Invalid`，不靜默**
//   套用任何值（不夾限、不回傳 NaN/Inf、不回傳無意義的巨大縮放）。
//
//   視差位移：offset = vanishing_point_direction * parallax_strength * (1 - scale(d))。
//   基準面（scale=1）位移為零；越遠（scale 越小）越朝消失點方向位移，模擬透視匯聚。
//
//   景深模糊(depth of field，可選，預設關閉)：depth_blur = blur_strength * |d - focal_depth|
//   （相對量，非像素半徑）；關閉時恆為 0。
//
// 不變量：物件「目前深度」與「目前透視強度」的組合恆維持有效（`set_depth` /
// `set_perspective_strength` 互相校驗，拒絕會使組合退化的設定），因此 `transform()` /
// `render_model()` 恆可安全取值，不需呼叫端額外檢查狀態。查任意假設深度（不修改狀態）則用
// `transform_for_depth()`，其回傳明確帶狀態碼，無效深度不靜默。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_28_PERSPECTIVE_ELEMENT_HPP
#define DS_ELEMENTS_E4_28_PERSPECTIVE_ELEMENT_HPP

#include "transform2d.hpp"  // E4-22（上游，可讀不可改）：ds::render::Transform2D / Vec2

namespace ds::elements {

// 操作結果碼 —— 與同子系統各 module 層單元同精神：明確、不靜默。
enum class PerspectiveStatus {
    Ok,       // 操作成功
    Invalid,  // 前置條件違反（非有限值 / 會使透視退化的深度或強度組合等）；不套用
};

// 消失點方向（具名九宮格；NFR-02：具名相對方向，非螢幕座標）。內部換算為單位方向向量
// （角落方向正規化為單位長度），供視差位移計算使用，不對外暴露任何座標數字。
enum class VanishingPoint {
    Center,
    Top,
    Bottom,
    Left,
    Right,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

// 透視退化判定的最小分母閾值：(1 + perspective_strength * depth) <= 此值視為退化
// （深度已達或超越與消失點重合的臨界點）。與 E4-22 `kSingularEpsilon` 同精神，但取較
// 寬鬆的物理意義閾值（避免縮放趨近無限大的極端退化情形）。
inline constexpr float kMinPerspectiveDenominator = 1e-3f;

// transform_for_depth() 的回傳：狀態 + 變形矩陣 + 等效縮放（僅在 status==Ok 時有效）。
// 明確以狀態回報無效深度，絕不靜默回傳無意義數值（同 E4-22 `InverseResult` 精神）。
struct PerspectiveTransformResult {
    PerspectiveStatus status = PerspectiveStatus::Invalid;
    ds::render::Transform2D transform;  // 僅在 ok() 為 true 時有效；無效時為單位矩陣佔位，勿用
    float scale = 0.0f;                 // 該深度換算後的等效縮放係數；無效時為 0，勿用
    bool ok() const { return status == PerspectiveStatus::Ok; }
};

// 透視渲染描述 —— 供後續相位的繪製層消費。
struct PerspectiveRenderModel {
    ds::render::Transform2D transform;  // 目前深度對應的透視變形矩陣（E4-22，含縮放 + 視差位移）
    float depth = 0.0f;                 // 目前深度（相對值，0 = 基準面）
    float scale = 1.0f;                 // 目前深度換算後的等效縮放係數（診斷用，衍生自 transform）
    float depth_blur = 0.0f;            // 景深模糊量（相對值，0 = 完全清晰；僅在啟用景深模糊時非零）
};

// ---------------------------------------------------------------------------
// PerspectiveElement —— 透視與景深元件：以 E4-22 變形矩陣模擬 2.5D 透視效果。
// 純邏輯、平台中立、不做真實繪製。
// ---------------------------------------------------------------------------
class PerspectiveElement {
public:
    PerspectiveElement() = default;

    // --- 深度（相對值）---
    // 設定目前深度。0 = 基準面；非有限值（NaN/Inf），或與目前透視強度組合會使透視退化
    // （見 kMinPerspectiveDenominator）→ `Invalid`（不套用，維持既有深度）。
    PerspectiveStatus set_depth(float depth);
    float depth() const noexcept { return depth_; }

    // --- 消失點（具名/相對）---
    // 設定消失點方向（具名九宮格；列舉恆合法，恆回 `Ok`）。
    PerspectiveStatus set_vanishing_point(VanishingPoint vanishing_point) noexcept;
    VanishingPoint vanishing_point() const noexcept { return vanishing_point_; }

    // --- 視差強度 ---
    // 設定視差位移幅度比例係數。非負有限值；否則 `Invalid`（不套用，維持既有值）。
    // 0 = 只有縮放、無視差位移。
    PerspectiveStatus set_parallax_strength(float strength);
    float parallax_strength() const noexcept { return parallax_strength_; }

    // --- 透視強度 ---
    // 設定深度換算縮放的陡峭程度係數。正有限值，且須使目前深度不致退化；否則 `Invalid`
    // （不套用，維持既有值——保持「目前狀態恆有效」不變量）。
    PerspectiveStatus set_perspective_strength(float strength);
    float perspective_strength() const noexcept { return perspective_strength_; }

    // --- 景深模糊（可選，預設關閉）---
    void set_depth_of_field_enabled(bool enabled) noexcept { depth_of_field_enabled_ = enabled; }
    bool depth_of_field_enabled() const noexcept { return depth_of_field_enabled_; }
    // 設定對焦深度（相對值）。非有限值 → `Invalid`（不套用，維持既有值）。
    PerspectiveStatus set_focal_depth(float focal_depth);
    float focal_depth() const noexcept { return focal_depth_; }
    // 設定模糊強度係數。非負有限值；否則 `Invalid`（不套用，維持既有值）。
    PerspectiveStatus set_blur_strength(float strength);
    float blur_strength() const noexcept { return blur_strength_; }

    // --- 核心查詢：給定任意假設深度求透視變形（純函式，不改內部狀態）---
    // 依目前消失點 / 視差強度 / 透視強度，計算「若元件位於該深度」應套用的透視變形矩陣：
    //   translate(offset) ∘ scale(s, s)
    // 即「先縮放，再依視差位移」。無效深度（非有限、或使透視退化）→ 明確回傳
    // `PerspectiveStatus::Invalid`（矩陣為單位矩陣佔位，勿用），不靜默。
    PerspectiveTransformResult transform_for_depth(float depth) const;

    // --- 目前深度對應的變形矩陣 ---
    // 等同 transform_for_depth(depth()).transform；因「目前狀態恆有效」不變量，恆可安全取值。
    ds::render::Transform2D transform() const;

    // --- 渲染描述 ---
    // 產出透視渲染描述（變形矩陣 + 深度 + 等效縮放 + 景深模糊量）。
    PerspectiveRenderModel render_model() const;

private:
    VanishingPoint vanishing_point_ = VanishingPoint::Center;
    float depth_ = 0.0f;
    float perspective_strength_ = 1.0f;
    float parallax_strength_ = 1.0f;
    bool depth_of_field_enabled_ = false;
    float focal_depth_ = 0.0f;
    float blur_strength_ = 1.0f;
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_28_PERSPECTIVE_ELEMENT_HPP
