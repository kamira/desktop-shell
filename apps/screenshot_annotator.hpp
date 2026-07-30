// apps/c4_02/screenshot_annotator.hpp — C4-02 截圖與標註（artifact 層 / apps，相位 1）
//
// 「截圖與標註」（screenshot & annotate）：擷取螢幕指定區域後在其上標註箭頭 / 框 / 文字，
// 匯出含影像參照（或存檔路徑）與標註數量的結果 bundle。本單元不是新引擎邏輯，而是把三個
// 已合併的擴充點 / 元件**組裝**成單一應用 + 行為（`capture(region)` / `annotate_*(shape)` /
// `export_result()`）：
//
//   - E3-11（`ds::actuators::ScreenCaptureBackend` / `CaptureSpec` / `CaptureRegion` /
//     `CaptureResult`）：以**注入式**擷取後端擷取指定區域（相位 1：`NullScreenCaptureBackend`
//     遵循請求區域尺寸回注入假影像，絕不觸碰真實螢幕）。
//   - E4-20（`ds::elements::VectorGraphic` / `Shape` / `build_shape_path`）：把箭頭
//     （相位 1 簡化為直線，箭頭符號由後續相位的繪製層補上）/ 框（矩形）標註轉為向量渲染
//     描述；文字標註不經向量路徑（本層無字形渲染），僅記錄位置 + 內容。
//   - E4-30（`ds::elements::DimOverlayElement`）：全螢幕調光覆蓋，本單元借它組出**選取框
//     覆蓋**——顯示覆蓋層並在目前擷取區域挖一個具名「不調光」框選區域，凸顯剛截取的畫面。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實螢幕擷取、無真實影像合成 / 匯出
// （`export_result()` 只產出描述性結果 bundle，不寫真實檔案）、無平台分支（無 `#ifdef` /
// win32 / cocoa）。核心 API 無數字 z-order（NFR-02：選取框覆蓋以具名挖洞表達，非疊放層
// 級）；擷取區域直接沿用 E3-11 既有 `CaptureRegion`（其 x/y/width/height 為擷取請求語意，
// 非 surface 疊放座標，且該型別已於上游合併定案）；標註座標沿用 E4-20 `Point`（元件本地
// 座標系，非螢幕像素）。任何無效操作（未設定擷取後端、無效擷取區域、尚未擷取即標註 /
// 框選、退化標註形狀、空文字標註）一律明確回傳具名結果，不靜默。
#ifndef DS_APPS_C4_02_SCREENSHOT_ANNOTATOR_HPP
#define DS_APPS_C4_02_SCREENSHOT_ANNOTATOR_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "dim_overlay.hpp"              // E4-30（上游，可讀不可改）：DimOverlayElement / DimStatus
#include "screen_capture_actuator.hpp"  // E3-11（上游，可讀不可改）：ScreenCaptureBackend / CaptureSpec / CaptureRegion / CaptureResult
#include "vector_graphic.hpp"           // E4-20（上游，可讀不可改）：VectorGraphic / Shape / build_shape_path / Point

namespace ds::apps {

// capture() 的具名結果。
enum class CaptureStatus {
    Ok,             // 成功擷取
    NoBackend,      // 未設定擷取後端（backend 為 null）
    InvalidRegion,  // 擷取區域無效（寬 / 高非正）
};

const char* to_string(CaptureStatus s) noexcept;

// annotate_*() 的具名結果。
enum class AnnotateStatus {
    Ok,         // 成功新增標註
    NoCapture,  // 尚未成功擷取過（無截圖可標註）
    Invalid,    // 標註本身無效（退化形狀 / 空文字 / 非有限座標）
};

const char* to_string(AnnotateStatus s) noexcept;

// 標註種類。
enum class AnnotationKind {
    Arrow,  // 箭頭（相位 1 簡化為直線，箭頭符號由後續相位繪製層補上）
    Box,    // 框（矩形外框）
    Text,   // 文字（不經向量路徑，僅位置 + 內容）
};

const char* to_string(AnnotationKind k) noexcept;

// 選取框覆蓋所在具名挖洞區域名（NFR-02：具名，非絕對座標 / 數字 z-order）。一次擷取結果
// 對應單一選取框，故固定一個名字（不同於 C4-03 依取樣點命名多個挖洞）。
inline constexpr const char* kSelectionRegionName = "region.selection";

// 標註的預設描邊寬度（元件本地單位，非螢幕像素；沿用 E4-20 StrokeStyle 的單位語意）。
inline constexpr double kAnnotationStrokeWidth = 2.0;

// ---------------------------------------------------------------------------
// Annotation —— 一筆標註快照。
//
// Arrow / Box：經 E4-20 `build_shape_path` + `VectorGraphic` 產出的向量渲染描述（`render`
// 非空）。Text：不建向量路徑（本層無字形渲染），`render` 保持預設（`empty=true`），改以
// `text` 承載內容。`anchor` 為標註定位點（Arrow=起點 / Box=左上角 / Text=文字位置）。
// ---------------------------------------------------------------------------
struct Annotation {
    AnnotationKind kind = AnnotationKind::Box;
    ds::elements::VectorRenderModel render;  // Arrow/Box 有路徑；Text 恆空（render.empty=true）
    ds::elements::Point anchor;              // 定位點（元件本地座標系，NFR-02）
    std::string text;                        // 僅 Text 使用；其餘為空字串
};

// ---------------------------------------------------------------------------
// ExportResult —— 一次匯出的結果 bundle。
//
// 相位 1：純描述性彙整（擷取尺寸 + 影像參照或存檔路徑 + 標註數量），不做真實影像合成，
// 不寫真實檔案。尚未成功擷取過 → `ok=false`，其餘欄位維持預設值（不假裝有資料）。
// ---------------------------------------------------------------------------
struct ExportResult {
    bool ok = false;
    std::int64_t width = 0;
    std::int64_t height = 0;
    std::string image_ref;         // 沿用 E3-11 CaptureResult::image_ref（記憶體參照）
    std::string path;              // 沿用 E3-11 CaptureResult::path（存檔路徑；與 image_ref 二擇一）
    std::size_t annotation_count = 0;
};

// ---------------------------------------------------------------------------
// ScreenshotAnnotatorApp —— 截圖與標註應用：組裝 E3-11（螢幕擷取）+ E4-20（向量標註）+
// E4-30（選取框覆蓋）。
//
// 以 `shared_ptr` 持有 E3-11 `ScreenCaptureBackend`（可 null，相位 1 注入式擷取後端；
// null 時每次 `capture` 回 `NoBackend`，不崩）；以參考持有 E4-30 `DimOverlayElement`
// （不取得所有權，須比本物件活得久，沿用 C4-03 的組裝相依風格）。
// ---------------------------------------------------------------------------
class ScreenshotAnnotatorApp {
public:
    ScreenshotAnnotatorApp(std::shared_ptr<ds::actuators::ScreenCaptureBackend> backend,
                           ds::elements::DimOverlayElement& selection);

    // --- 行為：擷取（E3-11）---

    // 擷取指定區域：
    //   - 區域無效（寬 / 高非正）→ `InvalidRegion`，**不動**既有擷取狀態（不靜默清空已成功
    //     擷取過的畫面）。
    //   - 未設定後端（backend 為 null）→ `NoBackend`，同樣不動既有狀態。
    //   - 成功 → `Ok`：`current_capture()` 更新為本次結果；**清空既有標註**（新擷取開新的
    //     標註 session，不殘留前次標註）；若選取框覆蓋目前作用中，一併收起（挖洞名對應
    //     前次擷取，換畫面後不再有效，避免殘留誤導）。
    // `save_path` 非空時比照 E3-11 語意：結果帶 `path`（存檔路徑）而非 `image_ref`。
    CaptureStatus capture(const ds::actuators::CaptureRegion& region,
                          const std::string& save_path = "");

    bool has_backend() const noexcept { return static_cast<bool>(backend_); }
    bool has_capture() const noexcept { return has_capture_; }
    // 目前（最近一次成功）擷取結果。前提 `has_capture()`；從未成功擷取過則回預設值。
    const ds::actuators::CaptureResult& current_capture() const noexcept { return capture_; }

    // --- 行為：標註（E4-20）---

    // 標箭頭：以直線（`Shape::make_line`）表達方向（相位 1 簡化，無箭頭符號）。
    //   - 尚未成功擷取過 → `NoCapture`（無畫面可標註）。
    //   - 零長度線段 / 非有限座標 → `Invalid`（委派 E4-20 `build_shape_path` 判定退化）。
    AnnotateStatus annotate_arrow(ds::elements::Point from, ds::elements::Point to);

    // 標框：矩形外框（`Shape::make_rect`）。
    //   - 尚未成功擷取過 → `NoCapture`。
    //   - 零 / 負寬高或非有限值 → `Invalid`（委派 E4-20 判定退化）。
    AnnotateStatus annotate_box(ds::elements::Point origin, double width, double height);

    // 標文字：不經向量路徑，僅記錄位置 + 內容。
    //   - 尚未成功擷取過 → `NoCapture`。
    //   - 空字串或非有限位置座標 → `Invalid`（報錯不靜默）。
    AnnotateStatus annotate_text(ds::elements::Point position, std::string text);

    std::size_t annotation_count() const noexcept { return annotations_.size(); }
    const std::vector<Annotation>& annotations() const noexcept { return annotations_; }
    void clear_annotations() noexcept { annotations_.clear(); }

    // --- 行為：選取框覆蓋（E4-30 組裝）---

    // 顯示選取框覆蓋：秀出調光覆蓋層，並在固定具名區域 `kSelectionRegionName` 挖一個
    // 「不調光」洞，凸顯剛截取的畫面（其餘畫面調暗）。
    //   - 尚未成功擷取過（`has_capture()` 為 false）→ `DimStatus::Invalid`（無畫面可框選）。
    //   - per-pixel alpha 能力不可用（NFR-03，委派 E4-30）→ `DimStatus::Unsupported`。
    ds::elements::DimStatus show_selection();
    // 收起選取框覆蓋：移除挖洞（若有）並隱藏覆蓋層。恆安全，不需能力閘控（委派 E4-30）。
    ds::elements::DimStatus hide_selection();
    bool selection_visible() const;

    // --- 行為：匯出 ---

    // 匯出目前狀態的結果 bundle：尺寸 + 影像參照 / 存檔路徑 + 標註數量。尚未成功擷取過 →
    // `ok=false`（其餘欄位維持預設值，不假裝有資料）。
    ExportResult export_result() const;

private:
    std::shared_ptr<ds::actuators::ScreenCaptureBackend> backend_;
    ds::elements::DimOverlayElement& selection_;

    bool has_capture_ = false;
    ds::actuators::CaptureResult capture_{};
    std::vector<Annotation> annotations_;
    bool selection_active_ = false;  // 選取框挖洞目前是否作用中（供換擷取時安全收起）。
};

}  // namespace ds::apps

#endif  // DS_APPS_C4_02_SCREENSHOT_ANNOTATOR_HPP
