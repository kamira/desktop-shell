// E4-24 反鋸齒與內距 — 平台中立渲染設定介面
//
// 語意：提供「反鋸齒（anti-aliasing）與內距（padding / inset）」的**渲染設定**——
//   - 反鋸齒模式（`AntiAliasMode`：none / grayscale / subpixel，具名，非數字）。
//   - 渲染品質（`RenderQuality`：low / medium / high，具名等級，供反鋸齒取樣等成本 vs 畫質權衡）。
//   - 內容內距（`Insets`：上 / 右 / 下 / 左），每一側可為**正規化比例**（相對容器尺寸的
//     [0,1] 比例）或**具名間距權杖**（`SpacingToken`：xsmall..xlarge），兩者皆與絕對畫面
//     座標無關（NFR-02：不用硬編像素 / 不用數字 z-order）。
//   - `RenderStyle{aa, quality, insets}`：宣告式渲染樣式輸入。
//   - `RenderStyleService::apply()` / `render_model()`：驗證 + 正規化輸入，產出可供渲染層
//     消費的**渲染設定描述**（`RenderModel`：內距一律已解析為正規化比例）。
//
// 本單元屬 engine 層（平台中立純邏輯），**不繪任何實際像素、不綁真實繪圖 API、不碰任何 OS**：
// 是宣告式的渲染設定計算——「決定要怎麼設定」而非「執行繪製」。
//
// 建於上游 E1-03 `ds::kernel::AlphaSurfaceService` 所立下的設計精神之上（具名鍵、正規化比例、
// 明確狀態回報不靜默）：本單元的渲染樣式同樣不依附任何特定 surface 實體，是可獨立驗證 /
// 套用的純設定物件。
//
// 相位 1 硬約束：無 `#ifdef` / `win32` / `cocoa` / 真實繪圖 API；無效輸入一律以狀態碼明確
// 回報，不靜默吞掉或給出垃圾值。
#ifndef DS_RENDER_E4_24_RENDER_STYLE_HPP
#define DS_RENDER_E4_24_RENDER_STYLE_HPP

namespace ds::render {

// --- 反鋸齒模式（具名，非數字）---
enum class AntiAliasMode {
    None,       // 不做反鋸齒：邊緣銳利、成本最低
    Grayscale,  // 灰階反鋸齒：一般點陣 / 向量繪製常見的邊緣柔化
    Subpixel,   // 次像素反鋸齒：依 RGB 子像素排列取樣，銳利度較高但對背景色較敏感
};

// 反鋸齒模式是否為已知合法值（防禦性檢查：enum 底層值可被硬轉出範圍，如
// `static_cast<AntiAliasMode>(99)`；核心 API 對此類無效輸入一律明確報錯，不靜默）。
bool is_valid(AntiAliasMode mode);

// --- 渲染品質（具名等級，非數字）---
// 供反鋸齒 / 取樣等渲染成本與畫質之間的整體權衡設定；不表達任何座標或次序。
enum class RenderQuality {
    Low,     // 低品質：效能優先（低階裝置 / 大量重繪場景）
    Medium,  // 中品質：預設平衡
    High,    // 高品質：畫質優先
};

bool is_valid(RenderQuality quality);

// --- 具名間距權杖 ---
// 供內距以「具名」而非絕對像素表達（NFR-02）。每個權杖對應一個固定的正規化比例
// （見 render_style.cpp 的 `resolve_spacing_token()`），與容器實際尺寸解耦。
enum class SpacingToken {
    None,    // 0 間距
    XSmall,
    Small,
    Medium,
    Large,
    XLarge,
};

bool is_valid(SpacingToken token);

// 內距值的表達單位：具名權杖，或正規化比例。
enum class InsetUnit {
    Proportion,  // 使用 `InsetValue::proportion`：[0,1] 正規化比例（相對容器尺寸，非絕對像素）
    Named,       // 使用 `InsetValue::token`：具名間距權杖，經 `resolve_spacing_token()` 解析
};

bool is_valid(InsetUnit unit);

// 單側內距值 —— 純資料，具名或比例二擇一（由 `unit` 指示哪個欄位有效）。
struct InsetValue {
    InsetUnit unit = InsetUnit::Proportion;
    float proportion = 0.0f;              // unit == Proportion 時有效；正規化 [0,1]
    SpacingToken token = SpacingToken::None;  // unit == Named 時有效

    // 具名工廠：以正規化比例建構。
    static InsetValue from_proportion(float value);
    // 具名工廠：以具名間距權杖建構。
    static InsetValue from_token(SpacingToken value);
};

// 內容內距（上 / 右 / 下 / 左）—— 純資料，NFR-02 相對表達（無絕對座標 / 無數字 z-order）。
struct Insets {
    InsetValue top;
    InsetValue right;
    InsetValue bottom;
    InsetValue left;

    // 零內距（四側皆 0）。
    static Insets none();
    // 四側套用同一個內距值。
    static Insets uniform(InsetValue value);
    // 分別指定水平（左右）與垂直（上下）內距值。
    static Insets symmetric(InsetValue horizontal, InsetValue vertical);
};

// 宣告式渲染樣式輸入（未驗證 / 未正規化）：反鋸齒模式 + 品質 + 內距設定。
struct RenderStyle {
    AntiAliasMode aa = AntiAliasMode::Grayscale;
    RenderQuality quality = RenderQuality::Medium;
    Insets insets = Insets::none();
};

// `RenderStyleService::apply()` 的操作結果碼 —— 與其他子系統的 Status 同語意
// （平台中立、跨後端一致；明確狀態不靜默）。
enum class RenderConfigStatus {
    Ok,       // 驗證通過並已套用；render_model() 回傳最新設定描述
    Invalid,  // 前置條件不滿足（列舉值超出已知範圍、內距為非有限值等）；不更新目前設定
};

// 已驗證 / 正規化後的渲染設定描述 —— `RenderStyleService::apply()` 的產出。
// 內距一律已解析為正規化比例 [0,1]（具名權杖已查表換算），供渲染層直接消費。
struct RenderModel {
    AntiAliasMode aa = AntiAliasMode::Grayscale;
    RenderQuality quality = RenderQuality::Medium;
    float inset_top = 0.0f;
    float inset_right = 0.0f;
    float inset_bottom = 0.0f;
    float inset_left = 0.0f;
};

// 具名間距權杖 → 正規化比例（相對容器尺寸；非絕對像素）的查表換算。
// `SpacingToken::None` 之外的權杖回傳單調遞增的正數比例。
float resolve_spacing_token(SpacingToken token);

// ---------------------------------------------------------------------------
// RenderStyleService —— 反鋸齒與內距渲染設定的驗證 / 套用服務層。
//
// 純渲染設定邏輯：不建立、不持有任何 surface 或繪圖資源，僅驗證與正規化一份
// `RenderStyle`，產出可供渲染層消費的 `RenderModel` 描述。
// ---------------------------------------------------------------------------
class RenderStyleService {
public:
    // 驗證並套用一份渲染樣式：
    //   - `style.aa` / `style.quality` 非已知列舉值 → Invalid，不更新目前設定。
    //   - `style.insets` 任一側為 `InsetUnit::Named` 但 `token` 非已知列舉值 → Invalid。
    //   - `style.insets` 任一側為 `InsetUnit::Proportion` 但 `proportion` 非有限值
    //     （NaN / Inf）→ Invalid，不更新目前設定。
    //   - `proportion` 有限但超出 [0,1] → **夾限**至 [0,1]（非錯誤；與 E1-03 opacity 同精神），
    //     成功套用。
    //   - 驗證通過 → Ok，內距一律解析為正規化比例並存入目前設定（`render_model()` 可查詢）。
    RenderConfigStatus apply(const RenderStyle& style);

    // 目前已套用的渲染設定描述；`apply()` 從未成功過則回 nullptr（不可查詢未套用狀態）。
    // 指標於本服務物件存活期間、且未被下一次成功 `apply()` 覆寫前有效。
    const RenderModel* render_model() const;

    // 便利查詢：是否已有成功套用過的渲染設定。
    bool has_model() const { return has_model_; }

private:
    bool has_model_ = false;
    RenderModel model_{};
};

}  // namespace ds::render

#endif  // DS_RENDER_E4_24_RENDER_STYLE_HPP
