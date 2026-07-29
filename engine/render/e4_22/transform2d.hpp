// E4-22 2D 變形矩陣 — 平台中立純數學
//
// 提供渲染層做 2D 仿射變形（平移 / 旋轉 / 縮放 / 傾斜）所需的數學工具：
//   - `Vec2`：2D 點 / 向量的純資料表達。
//   - `Transform2D`：2x3 仿射矩陣（隱含末列 [0 0 1] 的 3x3），支援
//       * 單位矩陣 `identity()`
//       * 基本變形工廠 `translate` / `rotate` / `scale` / `shear`
//       * 矩陣組合 `compose()` / `operator*`（右運算元先套用）
//       * 點 `apply_point()` / 向量 `apply_vector()` 變換
//       * 反矩陣 `inverse()`（奇異矩陣**明確回報**不靜默）
//
// engine 層 / 繪製基座：**純數學**，不綁真實繪圖後端、不碰任何 OS、無平台分支
//（無 `#ifdef _WIN32` / win32 / cocoa）。確定性——相同輸入必得相同輸出，可完全單元測試。
//
// 這是**相對變形數學**（平移量 / 角度 / 縮放比 / 傾斜量），非絕對座標定位：API 不暴露任何
// 硬編畫面座標或數字 z-order（NFR-02 針對的是核心 API 硬編絕對座標；通用變形矩陣合法）。
// 座標系採業界通用的 2D 仿射慣例（同 SVG / Canvas 的 matrix(a,b,c,d,e,f)）：
//     | a  c  e |     點 (x,y) 變換為
//     | b  d  f |         (a*x + c*y + e,  b*x + d*y + f)
//     | 0  0  1 |
#ifndef DS_RENDER_E4_22_TRANSFORM2D_HPP
#define DS_RENDER_E4_22_TRANSFORM2D_HPP

namespace ds::render {

// 2D 點 / 向量 —— 純資料。點與向量的差別只在變換時是否套用平移（見 apply_point / apply_vector）。
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// 反矩陣結果狀態碼（平台中立、與其他子系統的 Status 同語意）。
enum class TransformStatus {
    Ok,        // 可逆，matrix 為有效反矩陣
    Singular,  // 奇異（行列式 ≈ 0，不可逆）；明確回報，呼叫端據此處理，不靜默給出垃圾值
};

// 求反的預設奇異判定閾值：|det| ≤ 此值視為奇異。呼叫端可於 inverse() 覆寫。
inline constexpr float kSingularEpsilon = 1e-12f;

class Transform2D;

// inverse() 的回傳：狀態 + 反矩陣（僅在 status==Ok / ok() 為 true 時有效）。
// 定義於 Transform2D 之後（完整型別於下方補齊）。
struct InverseResult;

// 2D 仿射變形矩陣 —— 以 6 個係數存 2x3（末列隱含 [0 0 1]）。
//
// 係數對應 | a c e ; b d f ; 0 0 1 |。所有工廠皆為**相對變形**（不含絕對座標）。
class Transform2D {
public:
    // 預設建構即單位矩陣（恆等變換）。
    Transform2D() = default;

    // 直接以 6 係數建構（進階 / 內部用）；一般請用下列具名工廠。
    Transform2D(float a, float b, float c, float d, float e, float f)
        : a_(a), b_(b), c_(c), d_(d), e_(e), f_(f) {}

    // --- 具名工廠（相對變形數學）---
    // 單位矩陣：恆等變換，apply_point(p) == p。
    static Transform2D identity();
    // 平移 (tx, ty)：點加上位移；為相對位移量，非絕對座標。
    static Transform2D translate(float tx, float ty);
    // 旋轉 radians（逆時針為正，標準數學慣例）：繞原點旋轉。
    static Transform2D rotate(float radians);
    // 縮放 (sx, sy)：各軸獨立縮放比。
    static Transform2D scale(float sx, float sy);
    // 均勻縮放便利多載。
    static Transform2D scale(float s);
    // 傾斜（剪切）：shx = x 隨 y 的傾斜量、shy = y 隨 x 的傾斜量（tan 值，非角度）。
    static Transform2D shear(float shx, float shy);

    // --- 組合 ---
    // 回傳 (*this) ∘ rhs：**先套用 rhs，再套用 *this**。
    // 即 result.apply_point(p) == this->apply_point(rhs.apply_point(p))。
    Transform2D compose(const Transform2D& rhs) const;
    // 同 compose：a * b 表「先 b 後 a」，符合矩陣乘法慣例。
    Transform2D operator*(const Transform2D& rhs) const { return compose(rhs); }

    // --- 變換 ---
    // 當作**點**變換：套用線性部分 + 平移。
    Vec2 apply_point(const Vec2& p) const;
    // 當作**向量 / 方向**變換：只套用線性部分，忽略平移（位移對方向無意義）。
    Vec2 apply_vector(const Vec2& v) const;

    // --- 反矩陣 ---
    // 行列式（線性部分）。為 0（在閾值內）即不可逆。
    float determinant() const { return a_ * d_ - b_ * c_; }
    // 是否可逆（|det| > epsilon）。
    bool is_invertible(float epsilon = kSingularEpsilon) const;
    // 求反矩陣。可逆 → { Ok, 反矩陣 }；奇異 → { Singular, 單位矩陣（佔位，勿用） }。
    // 明確以狀態回報奇異，絕不靜默回傳無意義數值。
    InverseResult inverse(float epsilon = kSingularEpsilon) const;

    // --- 比較（供測試 / 收斂判斷）---
    // 各係數在 epsilon 內近似相等。
    bool approx_equals(const Transform2D& other, float epsilon = 1e-5f) const;

    // --- 係數存取（唯讀）---
    float a() const { return a_; }
    float b() const { return b_; }
    float c() const { return c_; }
    float d() const { return d_; }
    float e() const { return e_; }
    float f() const { return f_; }

private:
    // 單位矩陣係數。
    float a_ = 1.0f;
    float b_ = 0.0f;
    float c_ = 0.0f;
    float d_ = 1.0f;
    float e_ = 0.0f;
    float f_ = 0.0f;
};

// inverse() 的回傳型別（完整定義；Transform2D 至此已完整）。
struct InverseResult {
    TransformStatus status = TransformStatus::Singular;
    Transform2D matrix;  // 僅在 ok() 為 true 時有效；奇異時為單位矩陣佔位，勿用
    bool ok() const { return status == TransformStatus::Ok; }
};

}  // namespace ds::render

#endif  // DS_RENDER_E4_22_TRANSFORM2D_HPP
