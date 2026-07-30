// E4-24 反鋸齒與內距 — 實作
//
// 純渲染設定驗證 / 正規化邏輯；不含任何平台分支、真實繪圖 API、OS 呼叫。
#include "render_style.hpp"

#include <algorithm>  // std::clamp
#include <cmath>      // std::isfinite

namespace ds::render {

// --- 列舉合法性檢查 ---
// 寫法刻意窮舉所有已知列舉子，不寫 default：底層值落在已知範圍外（例如呼叫端以
// static_cast 硬轉出界）時，switch 不命中任何 case，落到函式尾端回傳 false，
// 明確視為「不合法」而非靜默放行或未定義行為。
bool is_valid(AntiAliasMode mode) {
    switch (mode) {
        case AntiAliasMode::None:
        case AntiAliasMode::Grayscale:
        case AntiAliasMode::Subpixel:
            return true;
    }
    return false;
}

bool is_valid(RenderQuality quality) {
    switch (quality) {
        case RenderQuality::Low:
        case RenderQuality::Medium:
        case RenderQuality::High:
            return true;
    }
    return false;
}

bool is_valid(SpacingToken token) {
    switch (token) {
        case SpacingToken::None:
        case SpacingToken::XSmall:
        case SpacingToken::Small:
        case SpacingToken::Medium:
        case SpacingToken::Large:
        case SpacingToken::XLarge:
            return true;
    }
    return false;
}

bool is_valid(InsetUnit unit) {
    switch (unit) {
        case InsetUnit::Proportion:
        case InsetUnit::Named:
            return true;
    }
    return false;
}

// --- InsetValue / Insets 具名工廠 ---
InsetValue InsetValue::from_proportion(float value) {
    InsetValue v;
    v.unit = InsetUnit::Proportion;
    v.proportion = value;
    return v;
}

InsetValue InsetValue::from_token(SpacingToken value) {
    InsetValue v;
    v.unit = InsetUnit::Named;
    v.token = value;
    return v;
}

Insets Insets::none() {
    Insets in;
    in.top = InsetValue::from_proportion(0.0f);
    in.right = InsetValue::from_proportion(0.0f);
    in.bottom = InsetValue::from_proportion(0.0f);
    in.left = InsetValue::from_proportion(0.0f);
    return in;
}

Insets Insets::uniform(InsetValue value) {
    Insets in;
    in.top = value;
    in.right = value;
    in.bottom = value;
    in.left = value;
    return in;
}

Insets Insets::symmetric(InsetValue horizontal, InsetValue vertical) {
    Insets in;
    in.top = vertical;
    in.bottom = vertical;
    in.left = horizontal;
    in.right = horizontal;
    return in;
}

// --- 具名間距權杖 → 正規化比例查表 ---
// 固定表：與容器實際尺寸無關，純粹是「具名等級」到「相對比例」的換算（NFR-02：
// 全程不涉及絕對像素）。刻意單調遞增，語意上 XSmall < Small < ... < XLarge。
float resolve_spacing_token(SpacingToken token) {
    switch (token) {
        case SpacingToken::None:
            return 0.0f;
        case SpacingToken::XSmall:
            return 0.01f;
        case SpacingToken::Small:
            return 0.02f;
        case SpacingToken::Medium:
            return 0.04f;
        case SpacingToken::Large:
            return 0.08f;
        case SpacingToken::XLarge:
            return 0.16f;
    }
    return 0.0f;  // 不應到達（is_valid() 先行把關）；保守回 0，不放大未定義輸入的影響
}

namespace {

// 解析單側內距值為正規化比例：
//   - unit 非已知合法值 → false（Invalid）。
//   - Named：token 非已知合法值 → false；否則查表換算（結果恆在 [0,1] 內，免夾限）。
//   - Proportion：非有限值（NaN / Inf）→ false；有限則夾限至 [0,1]（非錯誤）。
bool resolve_inset_value(const InsetValue& in, float& out) {
    if (!is_valid(in.unit)) {
        return false;
    }
    if (in.unit == InsetUnit::Named) {
        if (!is_valid(in.token)) {
            return false;
        }
        out = resolve_spacing_token(in.token);
        return true;
    }
    // InsetUnit::Proportion
    if (!std::isfinite(in.proportion)) {
        return false;
    }
    out = std::clamp(in.proportion, 0.0f, 1.0f);  // 內距夾限：有限但越界視為夾限而非錯誤
    return true;
}

}  // namespace

// --- RenderStyleService ---
RenderConfigStatus RenderStyleService::apply(const RenderStyle& style) {
    if (!is_valid(style.aa) || !is_valid(style.quality)) {
        return RenderConfigStatus::Invalid;  // 未知反鋸齒模式 / 品質列舉值：不更新目前設定
    }

    float top = 0.0f, right = 0.0f, bottom = 0.0f, left = 0.0f;
    if (!resolve_inset_value(style.insets.top, top) ||
        !resolve_inset_value(style.insets.right, right) ||
        !resolve_inset_value(style.insets.bottom, bottom) ||
        !resolve_inset_value(style.insets.left, left)) {
        return RenderConfigStatus::Invalid;  // 任一側內距無效（非有限值 / 未知權杖 / 未知單位）
    }

    model_.aa = style.aa;
    model_.quality = style.quality;
    model_.inset_top = top;
    model_.inset_right = right;
    model_.inset_bottom = bottom;
    model_.inset_left = left;
    has_model_ = true;
    return RenderConfigStatus::Ok;
}

const RenderModel* RenderStyleService::render_model() const {
    return has_model_ ? &model_ : nullptr;
}

}  // namespace ds::render
