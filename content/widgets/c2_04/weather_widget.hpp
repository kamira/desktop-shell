// content/widgets/c2_04/weather_widget.hpp — C2-04 天氣 widget（artifact 層 / 相位 1：組裝型 widget）
//
// 語意：桌面「天氣」widget —— 經 **E2-14** 注入式 `HttpTransport` 對一個端點發 GET 取回結構化
// （JSON）天氣資料，以 E2-14 自帶的 `parse_json` / `JsonPath` / `seek` 解析出溫度 / 描述 / 圖示
// 代碼三個欄位，經 **E7-10** 字串函式庫（`format` 樣板組字、`concat` 組請求 URL）格式化為可讀
// 文字，再以 **E4-01** `TextLayout` 把該文字排版為渲染描述、以 **E4-02** `ImageElement` 把圖示
// 代碼組裝為圖片渲染描述，兩者合成一份 `WeatherRenderModel` 供後續相位的繪製層消費。掛
// **C1-01**（`SkinProfile`）為桌面基底（具名圖層歸屬 / 輸入策略 / 透明外形 / 自由拖曳皆透傳
// 該基底，本單元不重造）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實網路（transport 為注入式，相位 1 僅
// `NullHttpTransport`）、無真實字型光柵化 / 影像解碼（`FontMetrics` / `ImageSource` 皆注入）、
// 無平台分支（無 `#ifdef` / win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02，全數透傳上游
// 具名模型）。任何無效操作（未設定即刷新、宣告式設定結構 / 具名值不合法、擷取失敗、結構化
// 解析失敗、尚未取得資料即顯示）一律回傳具名結果，不靜默。
//
// 依賴注入約定（皆不擁有其生命週期，須比本物件活得久）：
//   - `ds::kernel::KernelBackend&` / `ds::kernel::LayerStack&`：透傳給內部的 C1-01 `SkinProfile`
//     基底（同 C1-01 約定）。
//   - `std::shared_ptr<ds::sysinfo::HttpTransport>`：相位 1 為 `NullHttpTransport`；可為
//     nullptr（`refresh()` 保守回 `FetchFailed`，不崩）。
//   - `const ds::render::FontMetrics&`：E4-01 排版用字型度量，須存活於本物件之外。
//
// 命名空間 `ds::widgets`。
#ifndef DS_CONTENT_WIDGETS_C2_04_WEATHER_WIDGET_HPP
#define DS_CONTENT_WIDGETS_C2_04_WEATHER_WIDGET_HPP

#include <memory>
#include <string>

#include "document.hpp"        // E7-01（上游，可讀不可改）：ds::format::Value（宣告式 configure 輸入）
#include "http_fetch.hpp"      // E2-14（上游，可讀不可改）：HttpTransport / HttpResponse / JsonValue /
                                //   JsonPath / seek / parse_json（注入式 transport + 結構化解析）
#include "image_element.hpp"   // E4-02（上游，可讀不可改）：ImageElement / ImageRenderModel / MemoryImageSource
#include "skin_profile.hpp"    // C1-01（上游，可讀不可改）：SkinProfile（桌面基底）
#include "strings.hpp"         // E7-10（上游，可讀不可改）：ds::format::strings::format / concat（格式化）
#include "text_layout.hpp"     // E4-01（上游，可讀不可改）：TextLayout / FontMetrics / LayoutResult

namespace ds::widgets {

// 溫度顯示單位（NFR-02：具名，非數字係數）。
enum class TemperatureUnit {
    Celsius,
    Fahrenheit,
};

const char* to_string(TemperatureUnit u) noexcept;

// 天氣 widget 操作的具名結果（與上游各單元同精神：明確、不靜默）。
enum class WeatherStatus {
    Ok,           // 操作成功。
    Invalid,      // 前置條件不滿足：尚未 configure()、宣告式設定結構 / 具名值不合法、尚未取得
                  // 有效資料即 display()。
    FetchFailed,  // HTTP 擷取失敗：transport 為 nullptr（無後端）或回應非 2xx。
    ParseFailed,  // 回應 body 結構化解析失敗，或必要欄位（temp_c / description）缺失 / 型別不符。
};

const char* to_string(WeatherStatus s) noexcept;

// widget 設定（configure() 的解讀結果）。
struct WeatherConfig {
    std::string location;                          // 顯示用地點名稱（非空）。
    TemperatureUnit unit = TemperatureUnit::Celsius;
};

// 天氣 widget 目前顯示的渲染描述 —— 合成 E4-02 圖示渲染描述 + E4-01 文字渲染描述。
// 純資料，供後續相位的繪製層消費（本單元不繪製）。
struct WeatherRenderModel {
    bool has_data = false;                  // 是否曾成功 display() 過（false = 尚無資料）。
    std::string formatted_text;             // E7-10 format() 組出的可讀文字（如 "23°C  多雲"）。
    ds::render::LayoutResult text;          // E4-01 排版結果（相對佈局，NFR-02）。
    ds::elements::ImageRenderModel icon;    // E4-02 圖示渲染描述（icon 代碼缺失時 has_source=false）。
};

// ---------------------------------------------------------------------------
// WeatherWidget —— 天氣 widget：組裝 C1-01 + E2-14 + E7-10 + E4-01 + E4-02。
//
// 每個實例代表**一個**具名天氣 widget（如 "widget.weather.home"）。內部持有一個 C1-01
// `SkinProfile` 作為桌面基底（圖層 / 輸入 / 透明外形 / 拖曳全數透傳），並自持一個 E4-01
// `TextLayout`（綁定字型度量與文字 surface）。行為順序：`configure()` → `refresh()`
// （經注入 transport 擷取 + 解析）→ `display()`（格式化 + 排版 + 組裝渲染描述）。
// ---------------------------------------------------------------------------
class WeatherWidget {
public:
    // 圖示的名義固有尺寸（相位 1 不真的解碼影像；來源尺寸為描述性資料，供 E4-02 渲染描述使用）。
    static constexpr int kIconSize = 64;

    // 建構一個具名天氣 widget。
    //   id         widget 的具名識別碼；同時作為內部 C1-01 基底的 SurfaceId，並衍生文字 /
    //              圖示的具名目標 surface（"<id>.text" / "<id>.icon"）。
    //   backend/layers  透傳給內部 C1-01 SkinProfile（見其建構子約定），不取得所有權。
    //   transport  注入的 E2-14 HTTP 傳輸（相位 1 為 NullHttpTransport）；可為 nullptr。
    //   endpoint_url  要 GET 的天氣端點基底 URL（實際請求 URL 由 configure() 的 location 組成，
    //              見 request_url()）。
    //   metrics    E4-01 排版用字型度量（不取得所有權，須存活於本物件之外）。
    WeatherWidget(std::string id, ds::kernel::KernelBackend& backend,
                  ds::kernel::LayerStack& layers,
                  std::shared_ptr<ds::sysinfo::HttpTransport> transport,
                  std::string endpoint_url, const ds::render::FontMetrics& metrics);

    // --- C1-01 基底存取（圖層 / 輸入 / 透明外形 / 拖曳皆透傳，本單元不重造）---
    ds::profiles::SkinProfile& base() noexcept { return base_; }
    const ds::profiles::SkinProfile& base() const noexcept { return base_; }

    const std::string& id() const noexcept { return id_; }

    // --- 設定（Value:地點/單位）---
    // 解讀欄位（`definition` 須為 Map）：
    //   location: 字串，widget 顯示 / 請求用地點名稱（非空）。**必填**。
    //   unit:     字串，"celsius"（預設）或 "fahrenheit"。選填。
    // 定義非 Map、location 缺失 / 非字串 / 空字串、unit 非字串 / 非法具名值 → Invalid（不套用、
    // 不改既有設定）。成功 → Ok，config() 反映新設定；已取得的資料（若有）保留不清除。
    WeatherStatus configure(const ds::format::Value& definition);

    bool is_configured() const noexcept { return configured_; }
    const WeatherConfig& config() const noexcept { return config_; }

    // --- 刷新（經 E2-14 注入回應）---
    // 尚未 configure() → Invalid（不發請求）。
    // 依 request_url()（E7-10 concat 組成：endpoint_url + "?location=" + location）對注入的
    // transport 發 GET：
    //   - transport 為 nullptr、或回應非 2xx → FetchFailed（既有資料保留，不清除，供降級顯示）。
    //   - body 無法解析為 JSON、或非 Object、或必要欄位（temp_c 數值 / description 字串）缺失 /
    //     型別不符 → ParseFailed（既有資料保留）。icon 欄位選填：缺失或為 null 視為無圖示；
    //     若存在但非字串 → ParseFailed。
    //   - 全數成功 → Ok，內部天氣資料（攝氏溫度 / 描述 / icon 代碼）更新，has_data() 轉為 true。
    WeatherStatus refresh();

    // 依目前設定與 endpoint_url 組出的實際請求 URL（E7-10 concat；供診斷 / 測試核對）。
    std::string request_url() const;

    // --- 顯示（組裝 E4-01 文字 + E4-02 圖示渲染描述）---
    // 尚無有效資料（has_data()==false，即從未成功 refresh() 過）→ Invalid，render_model() 維持
    // 前一份（初始為空）。
    // 成功 → Ok：依 config().unit 換算顯示溫度、以 E7-10 `format()` 組出可讀文字、以 E4-01
    // `TextLayout::layout()` 排版、以 E4-02 `ImageElement` 組裝圖示渲染描述（icon 代碼為空則
    // has_source=false），寫入 render_model()。
    WeatherStatus display();

    const WeatherRenderModel& render_model() const noexcept { return model_; }

    // 是否曾成功 refresh() 取得有效天氣資料。
    bool has_data() const noexcept { return has_data_; }

    // --- 天氣資料查詢（反映最後一次成功 refresh()；has_data()==false 時為預設值）---
    double temperature_celsius() const noexcept { return temp_celsius_; }
    const std::string& description() const noexcept { return description_; }
    const std::string& icon_code() const noexcept { return icon_code_; }

    // --- 診斷（反映最後一次 refresh()）---
    int last_status() const noexcept { return last_status_; }

private:
    WeatherStatus apply_json(const ds::sysinfo::JsonValue& doc);

    std::string id_;
    ds::profiles::SkinProfile base_;  // C1-01：桌面基底（圖層 / 輸入 / 透明外形 / 拖曳）。

    std::shared_ptr<ds::sysinfo::HttpTransport> transport_;  // E2-14 注入式傳輸。
    std::string endpoint_url_;

    const ds::render::FontMetrics& metrics_;
    ds::render::TextLayout layout_;  // E4-01：自持，綁定 metrics_ 與 "<id>.text" 目標 surface。

    bool configured_ = false;
    WeatherConfig config_;

    bool has_data_ = false;
    double temp_celsius_ = 0.0;
    std::string description_;
    std::string icon_code_;

    int last_status_ = 0;

    WeatherRenderModel model_;
};

}  // namespace ds::widgets

#endif  // DS_CONTENT_WIDGETS_C2_04_WEATHER_WIDGET_HPP
