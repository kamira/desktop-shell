// E4-21 漸層填色 — 漸層(gradient)填色的「渲染描述模型」（module 層 / 子系統 elements）
//
// 語意：線性(Linear) / 放射狀(Radial)漸層填色——多個色停靠點(color stop：位置比例 [0,1] →
// 顏色)沿漸層軸排列，加上方向(角度)，最終產出**漸層渲染描述**供後續相位的繪製層消費。
// 這是桌面元件（背景、進度條、裝飾邊框等）漸層填色效果的視覺元件基礎。
//
// 相位 1**不做真實繪製**：本單元只產出**渲染描述**（`GradientRenderModel`）——不觸碰任何
// OS / 繪圖 API、無 `#ifdef` / `win32` / `cocoa`。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - 色停靠點**不以像素座標**表達，而以**正規化位置** `position ∈ [0,1]`（沿漸層軸的比例，
//     0 = 起點、1 = 終點）＋清單順序表達。
//   - 方向**不以向量 / 端點座標**表達，而以**具名列舉**（`GradientType`）＋**角度**
//     （`angle_degrees ∈ [0,360)`，度數是方向的具名量度，非座標）表達。
//   - 合成不透明度沿用上游 E1-03 `AlphaProfile`（opacity 為比例 [0,1]，非座標），描述此漸層
//     填色最終應如何與所在 surface 合成——仍符合 NFR-02。
//
// 不靜默失敗：
//   - 色停靠點位置越界（不在 [0,1]）或非有限值 → 回 `GradientStatus::InvalidPosition`，
//     **不**新增該停靠點、不夾限靜默改值。
//   - 顏色分量無效（不在 [0,1] 或非有限值）→ 回 `GradientStatus::InvalidColor`，同樣不新增。
//   - 角度非有限值 → 回 `GradientStatus::InvalidAngle`，不套用。
//   - 合成 opacity 非有限值 → 回 `GradientStatus::InvalidComposite`，不套用。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_21_GRADIENT_FILL_HPP
#define DS_ELEMENTS_E4_21_GRADIENT_FILL_HPP

#include <cstddef>
#include <vector>

#include "alpha_surface.hpp"  // E1-03（上游，可讀不可改）：AlphaProfile / AlphaMode（合成描述）

namespace ds::elements {

// 漸層種類（具名，非數字）。
enum class GradientType {
    Linear,  // 線性漸層：沿一條軸（由 angle_degrees 決定方向）由起點到終點漸變。
    Radial,  // 放射狀漸層：由中心向外漸變（半徑方向；angle_degrees 對放射漸層不影響取樣結果，
             // 但仍可設定 / 保留於渲染描述中，供繪製層需要時參照，例如橢圓長軸旋轉之類的延伸用途）。
};

// 一個顏色 —— 正規化 RGBA 分量 [0,1]（非 0-255 整數），比例值、不含任何座標語意。
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    bool operator==(const Color& o) const noexcept {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool operator!=(const Color& o) const noexcept { return !(*this == o); }
};

// 操作結果碼 —— 明確、不靜默。
enum class GradientStatus {
    Ok,               // 操作成功
    InvalidPosition,  // 停靠點位置非有限值或不在 [0,1]
    InvalidColor,     // 顏色分量非有限值或不在 [0,1]
    InvalidAngle,     // 角度非有限值
    InvalidComposite, // 合成 opacity 非有限值
};

// 一個色停靠點 —— 純資料、正規化位置、無絕對座標。
struct GradientStop {
    float position = 0.0f;  // 沿漸層軸的正規化位置 [0,1]（0 = 起點，1 = 終點）。
    Color color;
};

// 漸層填色的**渲染描述模型**——供後續相位的繪製層消費。相位 1 不含任何真實繪製。
struct GradientRenderModel {
    GradientType type = GradientType::Linear;
    float angle_degrees = 0.0f;           // 方向（度），[0,360)；具名量度，非座標。
    std::vector<GradientStop> stops;      // 依 position 由小到大排序（清單順序 = 沿軸順序）。
    ds::kernel::AlphaProfile composite;   // 合成描述（此漸層填色如何與所在 surface 合成）。
};

// ---------------------------------------------------------------------------
// GradientFill —— 漸層填色渲染描述模型的建構者。
//
// 累積多個色停靠點（依位置自動排序）、方向角度、合成不透明度，可對任一位置 `sample()`
// 內插取色，最後產出 `GradientRenderModel` 供繪製層消費。純邏輯、平台中立、無真實繪製。
// ---------------------------------------------------------------------------
class GradientFill {
public:
    explicit GradientFill(GradientType type = GradientType::Linear) : type_(type) {}

    // 漸層種類（線性 / 放射狀）。
    GradientType type() const noexcept { return type_; }

    // --- 色停靠點 ---
    // 新增一個色停靠點。position 須為有限值且在 [0,1] 內，否則 InvalidPosition（不新增）。
    // color 任一分量須為有限值且在 [0,1] 內，否則 InvalidColor（不新增）。
    // 成功 → Ok，並依 position 由小到大**自動排序**插入（同 position 依插入次序穩定排列）。
    GradientStatus add_stop(float position, const Color& color);

    // 目前累積的色停靠點數。
    std::size_t stop_count() const noexcept { return stops_.size(); }
    // 目前的色停靠點（依 position 由小到大排序，唯讀）。
    const std::vector<GradientStop>& stops() const noexcept { return stops_; }
    // 清空所有色停靠點（保留方向 / 合成設定）。
    void clear_stops() noexcept { stops_.clear(); }

    // --- 方向 ---
    // 設定漸層方向角度（度）。非有限值 → InvalidAngle（不套用）。
    // 成功 → Ok，並正規化至 [0,360)（如 370 → 10、-10 → 350）。
    GradientStatus set_angle(float degrees);
    float angle() const noexcept { return angle_degrees_; }

    // --- 合成不透明度（沿用 E1-03 AlphaProfile；opacity 為比例，符合 NFR-02）---
    // 設定此漸層填色的合成描述。opacity 非有限值 → InvalidComposite（不套用）；否則夾限至 [0,1]。
    GradientStatus set_composite(const ds::kernel::AlphaProfile& composite);
    const ds::kernel::AlphaProfile& composite() const noexcept { return composite_; }

    // --- 取樣 ---
    // 在沿漸層軸的正規化位置 t 取內插顏色。t 會先夾限至 [0,1]。
    //   - 無停靠點 → 回全透明黑 Color{0,0,0,0}（明確的安全預設值，非隨機未定義狀態）。
    //   - 僅一個停靠點 → 該顏色恆定回傳（與 t 無關）。
    //   - t 落在最早停靠點之前 → 回最早停靠點顏色（夾限）。
    //   - t 落在最晚停靠點之後 → 回最晚停靠點顏色（夾限）。
    //   - 否則 → 在相鄰兩停靠點間依位置比例做逐分量線性內插（r/g/b/a 各自內插）。
    Color sample(float t) const;

    // --- 渲染描述 ---
    // 產出目前狀態的渲染描述模型：種類、角度、已排序的停靠點清單、合成描述。
    GradientRenderModel render_model() const;

private:
    GradientType type_;
    float angle_degrees_ = 0.0f;
    std::vector<GradientStop> stops_;
    ds::kernel::AlphaProfile composite_{};  // 預設：PerPixel、opacity=1.0。
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_21_GRADIENT_FILL_HPP
