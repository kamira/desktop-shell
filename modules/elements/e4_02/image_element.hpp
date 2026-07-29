// E4-02 圖片元件 — 圖片顯示元件的「渲染描述模型」（module 層 / 子系統 elements）
//
// 語意：圖片顯示元件——載入圖片（以**可注入的圖片來源** `ImageSource`，提供尺寸／來源參照或
// 假資料）、選擇縮放模式(fill/fit/stretch/center/tile)、裁切、設定整體透明度，產出一份
// **圖片渲染描述**（來源參照 + 來源固有尺寸 + 縮放模式 + 正規化裁切 + 透明度 + 目標具名 surface）
// 供後續相位的繪製層消費。
//
// 相位 1**不做真實影像解碼／繪製**：本單元不觸碰任何 OS／繪圖 API、不連結 libpng/libjpeg、
// 無 `#ifdef` / `win32` / `cocoa`。圖片內容以**可注入的 `ImageSource` 抽象**表達——相位 1 只需
// 來源的**固有尺寸**與**具名參照**（如資源鍵），不需真正解出像素；提供 null／固定實作即可測。
//
// NFR-02（無絕對座標 / 無數字 z-order）：
//   - **目標**以**具名 SurfaceId**（字串）指涉，不用數字 handle / index，也不含螢幕 (x,y) 位置。
//   - **裁切**以**正規化比例** [0,1]（占來源影像的分數）表達，非螢幕像素座標。
//   - **縮放模式**為**具名列舉**（fill/fit/stretch/center/tile），非數字係數。
//   - **透明度**沿用上游 E1-03 `AlphaProfile`（opacity 為比例 [0,1]，非座標 / 非 z-order）。
//   - 來源的固有尺寸（`ImageDimensions`）是**影像自身的資料**（像 E4-20 的本地座標系），非螢幕
//     擺放位置；本單元完全不提供任何疊放層級 / z-order 欄位。
//
// 不靜默失敗：無效來源（`valid()==false`）、尺寸為零／負、非法裁切（超出 [0,1]、寬高非正、
// 非有限值）、空目標名、非有限不透明度一律回 `ImageStatus::Invalid`，**不**套用、**不**靜默改值。
//
// 命名空間 `ds::elements`。
#ifndef DS_ELEMENTS_E4_02_IMAGE_ELEMENT_HPP
#define DS_ELEMENTS_E4_02_IMAGE_ELEMENT_HPP

#include <string>

#include "alpha_surface.hpp"  // E1-03（上游，可讀不可改）：AlphaProfile / AlphaMode；
                              // 並透過其 include 傳遞 ds::kernel::SurfaceId（具名 surface）。

namespace ds::elements {

// 操作結果碼 —— 與同子系統各 module 層單元同精神：明確、不靜默。
enum class ImageStatus {
    Ok,       // 操作成功
    Invalid,  // 前置條件違反（無效來源、尺寸為零、非法裁切、空目標、非有限不透明度等）；不套用
};

// 縮放模式（具名，非數字係數；NFR-02）。描述來源影像如何映射到目標區域。
enum class ScaleMode {
    Fill,     // 等比縮放**填滿**目標（cover）；溢出部分被裁掉。
    Fit,      // 等比縮放**完整放入**目標（contain）；可能於邊緣留白。
    Stretch,  // 非等比拉伸**鋪滿**目標（可能變形）。
    Center,   // 以原始尺寸**置中**，不縮放。
    Tile,     // 以原始尺寸**平鋪重複**填滿目標。
};

// 來源影像的**固有尺寸**（影像自身資料的像素寬高，非螢幕座標 / 非擺放位置；NFR-02）。
// 相位 1 不解碼影像，此尺寸由注入的 `ImageSource` 提供（真實或假資料皆可）。
struct ImageDimensions {
    int width = 0;   // 固有像素寬；有效來源須 > 0。
    int height = 0;  // 固有像素高；有效來源須 > 0。
};

// ---------------------------------------------------------------------------
// ImageSource —— **可注入的圖片來源抽象**（相位 1 的解耦點）。
//
// 相位 1 不做真實影像解碼：呼叫端注入一個 `ImageSource`，本元件只向它索取**固有尺寸**與
// **具名來源參照**（如資源鍵 / 路徑鍵，僅作為描述字串，不被開啟 / 讀取）。真實後端上線後，
// 以同介面提供真正解碼出的尺寸即可，元件邏輯不變。
// ---------------------------------------------------------------------------
class ImageSource {
public:
    virtual ~ImageSource() = default;

    // 此來源是否有效（可載入）。無效來源會被 `ImageElement::set_source` 拒絕（回 Invalid）。
    virtual bool valid() const = 0;
    // 來源影像的固有尺寸（像素）。僅在 `valid()` 為真時有意義。
    virtual ImageDimensions dimensions() const = 0;
    // 具名來源參照（如資源鍵）。相位 1 僅作描述字串，不被開啟；NFR-02：具名、非數字 handle。
    virtual const std::string& reference() const = 0;
};

// 固定（記憶體）圖片來源 —— 供測試與相位 1 注入：直接以參照字串 + 固有尺寸建構假來源，
// 不做任何解碼。`valid()` = 參照非空且寬高皆 > 0。
class MemoryImageSource : public ImageSource {
public:
    MemoryImageSource(std::string reference, ImageDimensions dimensions)
        : reference_(std::move(reference)), dimensions_(dimensions) {}

    bool valid() const override {
        return !reference_.empty() && dimensions_.width > 0 && dimensions_.height > 0;
    }
    ImageDimensions dimensions() const override { return dimensions_; }
    const std::string& reference() const override { return reference_; }

private:
    std::string reference_;
    ImageDimensions dimensions_;
};

// 空（null）圖片來源 —— 明確表達「無影像 / 不可用」：恆 `valid()==false`、尺寸為零、參照為空。
// 供降級路徑與「無效來源報錯」情境。
class NullImageSource : public ImageSource {
public:
    bool valid() const override { return false; }
    ImageDimensions dimensions() const override { return {}; }
    const std::string& reference() const override { return empty_; }

private:
    std::string empty_;
};

// 正規化裁切矩形 —— 以**占來源影像的比例** [0,1] 表達（NFR-02：比例，非螢幕像素座標）。
// `(x,y)` 為裁切左上角於來源的分數位置，`width/height` 為裁切占來源的分數大小。
// 預設 `full()` = 整張影像 {0,0,1,1}。
struct CropRect {
    double x = 0.0;
    double y = 0.0;
    double width = 1.0;
    double height = 1.0;

    // 整張影像（不裁切）。
    static CropRect full() { return CropRect{0.0, 0.0, 1.0, 1.0}; }
};

// 圖片元件的**渲染描述模型** —— 供後續相位的繪製層消費。相位 1 不含任何真實解碼 / 繪製。
struct ImageRenderModel {
    bool has_source = false;             // 是否已載入有效來源（false = 空，明確不假裝有資料）。
    std::string source_reference;        // 具名來源參照（NFR-02：具名字串）。
    ImageDimensions source_dimensions;   // 來源固有尺寸（影像自身資料，非螢幕座標）。
    ScaleMode scale_mode = ScaleMode::Fit;
    CropRect crop;                       // 正規化裁切 [0,1]（比例，非絕對座標）。
    ds::kernel::AlphaProfile alpha;      // 整體透明度（opacity 比例 [0,1]）。
    ds::kernel::SurfaceId target;        // 具名目標 surface（空 = 未綁定；NFR-02：具名非數字）。
};

// ---------------------------------------------------------------------------
// ImageElement —— 圖片顯示元件：載入注入來源、選縮放模式、裁切、透明度，產出渲染描述模型。
// 純邏輯、平台中立、無真實影像解碼 / 繪製。
//
// 來源以**值語意載入**：`set_source` 於呼叫當下把來源的參照與固有尺寸**複製**進本元件，
// 因此元件不持有對 `ImageSource` 的懸空參照，來源物件可於載入後即銷毀。
// ---------------------------------------------------------------------------
class ImageElement {
public:
    ImageElement() = default;

    // --- 載入 / 卸載來源 ---
    // 載入（複製）一個注入來源的參照 + 固有尺寸。
    //   - `!source.valid()` 或尺寸寬高任一 <= 0 → `Invalid`，且**不**改變既有來源（不部分套用）。
    //   - 成功 → `Ok`，`has_source()` 轉為 true。
    ImageStatus set_source(const ImageSource& source);
    // 卸載目前來源（回到無來源狀態）。恆成功（釋放恆安全）。縮放模式 / 裁切 / 透明度 / 目標不變。
    void clear_source() noexcept;

    bool has_source() const noexcept { return has_source_; }
    const std::string& source_reference() const noexcept { return source_reference_; }
    ImageDimensions source_dimensions() const noexcept { return source_dimensions_; }

    // --- 縮放模式 ---
    // 設定縮放模式（具名列舉，恆合法）。回 `Ok`。
    ImageStatus set_scale_mode(ScaleMode mode) noexcept;
    ScaleMode scale_mode() const noexcept { return scale_mode_; }

    // --- 裁切（正規化 [0,1]）---
    // 設定正規化裁切。非有限值、寬高非正、任一邊界超出 [0,1]、或 x+width / y+height > 1
    // → `Invalid`（不套用）。成功 → `Ok`。
    ImageStatus set_crop(const CropRect& crop);
    // 清除裁切（回到整張影像 `CropRect::full()`）。恆成功。
    void clear_crop() noexcept { crop_ = CropRect::full(); }
    const CropRect& crop() const noexcept { return crop_; }

    // --- 透明度（沿用上游 AlphaProfile.opacity）---
    // 設定整體不透明度 [0,1]。非有限值 → `Invalid`（不套用）；成功 → `Ok`（自動 clamp 至 [0,1]）。
    ImageStatus set_opacity(float opacity);
    float opacity() const noexcept { return alpha_.opacity; }
    const ds::kernel::AlphaProfile& alpha() const noexcept { return alpha_; }

    // --- 目標具名 surface ---
    // 設定渲染目標（具名 SurfaceId）。空字串 → `Invalid`（不套用）。成功 → `Ok`。
    ImageStatus set_target(const ds::kernel::SurfaceId& target);
    // 解除目標綁定（回到未綁定的空目標）。恆成功。
    void clear_target() noexcept { target_.clear(); }
    const ds::kernel::SurfaceId& target() const noexcept { return target_; }

    // --- 渲染描述 ---
    // 產出目前狀態的渲染描述模型（來源參照 + 固有尺寸 + 縮放模式 + 裁切 + 透明度 + 目標）。
    // 未載入來源 → `has_source=false`、參照為空、尺寸為零（明確，不靜默假裝有資料）。
    ImageRenderModel render_model() const;

private:
    bool has_source_ = false;
    std::string source_reference_;
    ImageDimensions source_dimensions_{};
    ScaleMode scale_mode_ = ScaleMode::Fit;
    CropRect crop_ = CropRect::full();
    ds::kernel::AlphaProfile alpha_{};  // 預設 opacity = 1.0（完全不透明）。
    ds::kernel::SurfaceId target_;      // 空 = 未綁定。
};

}  // namespace ds::elements

#endif  // DS_ELEMENTS_E4_02_IMAGE_ELEMENT_HPP
