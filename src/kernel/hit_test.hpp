// E1-04 幾何命中測試（hit testing）— 介面 + 純幾何行為（platform 相位 1 = Mac / null 期）
//
// 語意：判定某點是否落在 surface / 形狀內，供輸入路由 / 點擊判定使用。提供：
//   - 矩形 / 圓角矩形 / 圓 / 多邊形 / path 的**點內判定**（point-in-shape，含邊界）。
//   - **alpha 命中**：逐像素 alpha surface 上透明處**不命中**（用上游 E1-03 的合成模式 /
//     不透明度 + 一個**注入式** per-pixel alpha 查詢；相位 1 無真實像素，故 oracle 由呼叫端注入）。
//   - **命中優先（topmost）**：多 surface 重疊時，依**具名圖層** + 宣告順序決定命中者。
//
// 相位 1（Mac / null 期）硬約束：
//   - **純幾何邏輯 + 注入式 alpha 查詢**，無真實視窗系統 / OS / 繪圖 API。
//   - 不得出現 `#ifdef _WIN32` / `win32` / `cocoa` 等平台分支；跨平台性由 API 面約束保證。
//   - **NFR-02**：座標一律**元件本地 / 相對**（非畫面絕對座標）；命中優先用**具名圖層**
//     （`SurfaceLayer`）+ 宣告順序，**無數字 z-order**。
//   - 無效形狀 / 座標**報錯不靜默**（回結構化 `HitStatus::Invalid`，不悄悄當作「未命中」）。
//
// 建於上游之上（可讀不可改）：
//   - E1-24 `null_backend.hpp`：`SurfaceId`（具名指涉）/ `SurfaceLayer`（具名圖層）/ `HitPolicy`。
//   - E1-03 `alpha_surface.hpp`：`AlphaMode` / `AlphaProfile`（合成模式 + 正規化不透明度）。
#ifndef DS_KERNEL_E1_04_HIT_TEST_HPP
#define DS_KERNEL_E1_04_HIT_TEST_HPP

#include <cstddef>
#include <functional>
#include <vector>

#include "alpha_surface.hpp"  // E1-03（上游，可讀不可改）：AlphaMode / AlphaProfile
#include "null_backend.hpp"   // E1-24（上游，可讀不可改）：SurfaceId / SurfaceLayer / HitPolicy

namespace ds::kernel {

// 元件本地 / 相對座標的 2D 點（NFR-02：**非畫面絕對座標**）。
//
// 原點為元件自身的本地原點；命中測試在形狀自身的本地座標系內進行，不涉及 surface 於螢幕上的
// 絕對位置——那由具名圖層 `SurfaceLayer` 表達，不以像素座標表達。
struct LocalPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// 形狀種類（具名分類）。
enum class ShapeKind {
    Rect,         // 軸對齊矩形：本地原點 (0,0) 到 (width, height)
    RoundedRect,  // 圓角矩形
    Circle,       // 圓
    Polygon,      // 簡單多邊形（even-odd 填充）
    Path,         // 一般路徑（顯式填充規則）
};

// 多邊形 / path 的填充規則（具名，非數字）。
enum class FillRule {
    EvenOdd,  // 奇偶規則（射線交點數的奇偶）
    NonZero,  // 非零環繞數（winding number）
};

// 一個待命中測試的幾何形狀 —— 純資料、本地座標、平台中立。
//
// 依 `kind` 取用對應欄位；建議以工廠函式（`make_rect` 等）建構，確保欄位一致。
struct Shape {
    ShapeKind kind = ShapeKind::Rect;

    // Rect / RoundedRect：以 (width, height) 表達範圍（extent），本地原點固定 (0,0)。
    float width = 0.0f;
    float height = 0.0f;
    // 僅 RoundedRect 使用；超過 min(width, height)/2 會被 clamp（非錯誤）。
    float corner_radius = 0.0f;

    // Circle：本地座標的圓心 + 半徑。
    LocalPoint center{};
    float radius = 0.0f;

    // Polygon / Path：本地座標的頂點序列（首尾自動閉合）。
    std::vector<LocalPoint> vertices;
    // 僅 Path 使用（Polygon 恆為 even-odd）。
    FillRule fill = FillRule::EvenOdd;
};

// --- 形狀工廠（保證欄位一致；座標皆本地 / 相對）---
Shape make_rect(float width, float height);
Shape make_rounded_rect(float width, float height, float corner_radius);
Shape make_circle(LocalPoint center, float radius);
Shape make_polygon(std::vector<LocalPoint> vertices);
Shape make_path(std::vector<LocalPoint> vertices, FillRule fill = FillRule::EvenOdd);

// 命中測試結果碼 —— 與 kernel 既有 Status 家族（如 E1-03 `AlphaStatus`）同語意，平台中立。
enum class HitStatus {
    Ok,       // 測試完成，結果見 `HitResult::inside`
    Invalid,  // 形狀 / 座標無效（非有限值、頂點數不足、負範圍、oracle 回非有限 alpha 等）——報錯不靜默
};

// 幾何命中測試結果（狀態 + 是否命中）。`inside` 僅在 `status == Ok` 時有意義。
struct HitResult {
    HitStatus status = HitStatus::Invalid;
    bool inside = false;
};

// 逐像素 alpha 查詢 —— **注入式** oracle：給定本地座標，回傳該點的正規化 per-pixel alpha [0,1]。
//
// 相位 1 無真實像素 / 繪圖後端，per-pixel alpha 資料由呼叫端 / 測試**注入**（保持平台中立、
// 與真實合成器解耦）。真實後端上線後，此 oracle 可改由後端 surface 的實際 alpha 通道供給。
using AlphaQuery = std::function<float(const LocalPoint&)>;

// 一個參與命中測試的具名 surface —— 具名指涉 + 本地形狀 + 具名圖層 + alpha 行為。
struct HitSurface {
    SurfaceId id;                               // 具名指涉（NFR-02：非數字 handle / index）
    Shape shape;                                // 本地座標形狀
    SurfaceLayer layer = SurfaceLayer::Normal;  // 具名圖層決定命中優先（NFR-02：非數字 z-order）
    HitPolicy hit = HitPolicy::Solid;           // Transparent = 命中穿透，永不命中本 surface
    AlphaProfile alpha{};                       // E1-03：合成模式 + 整體不透明度
    AlphaQuery alpha_query;                     // per-pixel alpha oracle（僅 PerPixel 模式使用；未注入視為 1.0）
    float alpha_threshold = 0.5f;               // 判定命中的最小有效 alpha（自動 clamp 至 [0,1]）
};

// 命中優先（topmost）結果。
struct TopmostHit {
    HitStatus status = HitStatus::Invalid;  // 任一 surface 形狀 / 座標無效即 Invalid
    bool hit = false;                       // 是否有任一 surface 命中
    SurfaceId id;                           // 命中之最上層 surface 的具名 id（未命中為空）
};

// ---------------------------------------------------------------------------
// HitTester —— 幾何命中測試（純幾何 + 注入式 alpha 查詢，無真實視窗系統）。
//
// 無狀態；所有方法皆 const、可安全併發呼叫。座標一律**本地 / 相對**，命中優先一律以**具名圖層**
// + 宣告順序決定（NFR-02：無畫面絕對座標、無數字 z-order）。無效輸入回 `Invalid`（報錯不靜默）。
// ---------------------------------------------------------------------------
class HitTester {
public:
    // 形狀是否有效：非有限值、頂點數不足（多邊形 / path < 3）、負範圍 / 負半徑 → 無效。
    bool is_valid(const Shape& shape) const;

    // 純幾何點內判定：本地點是否落在形狀內（**含邊界**）。
    // 無效形狀 / 非有限座標 → status=Invalid（報錯不靜默）；否則 status=Ok，inside 為判定結果。
    HitResult hit_test(const LocalPoint& point, const Shape& shape) const;

    // alpha 命中：幾何命中 **且**（PerPixel 模式時）該點有效 alpha ≥ 門檻。
    //   - 有效 alpha = per-pixel alpha(point) × 整體 opacity；`Opaque` 模式忽略 alpha 通道。
    //   - `HitPolicy::Transparent` 之 surface **永不命中**（命中穿透，落到其後）。
    //   - 形狀 / 座標無效、或注入 oracle 回非有限 alpha → status=Invalid（報錯不靜默）。
    HitResult hit_test_alpha(const LocalPoint& point, const HitSurface& surface) const;

    // 多 surface 重疊時的命中優先：回傳命中之**最上層** surface 的具名 id。
    //   - 優先序：具名圖層（Wallpaper < BelowNormal < Normal < Overlay < Topmost）為主序；
    //     同層以**宣告順序**後者為上（穩定順序，非數字 z-order）。
    //   - 逐一以 `hit_test_alpha` 判定；任一 surface 形狀 / 座標無效 → status=Invalid。
    //   - 無任何命中 → status=Ok、hit=false、id 為空。
    TopmostHit topmost_hit(const LocalPoint& point,
                           const std::vector<HitSurface>& surfaces) const;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_04_HIT_TEST_HPP
