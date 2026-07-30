// content/widgets/c2_10/web_widget.hpp — C2-10 網頁 widget
// （artifact 層 / 相位 1：純資料 / 邏輯組裝，無真實 GUI、無真實瀏覽器引擎）
//
// 「網頁」是一個桌面小工具（widget）：嵌入顯示一個網頁——可設定 URL、觀察載入狀態、以
// **注入式後端**取得內容。本單元不是新引擎邏輯，而是把兩個已合併的擴充點**組裝**成一個
// 具體 widget（lb=0，非其他單元依賴的基礎）：
//
//   - C1-01（`ds::profiles::SkinProfile`）：本 widget 的**桌面殼層基底**——可自由拖曳、具名
//     圖層歸屬、可互動、具透明外形；widget 的圖層 / 定位 / 拖曳皆委派其自身 `shell()`
//     （呼叫端可直接驅動 `shell().load_skin(...)` / `place` / `begin_drag` 等）。
//   - E10-05（`ds::ipc::WidgetHost`）：本 widget「網頁內容」的**隔離行程宿主**——相位 1
//     刻意不含真實瀏覽器引擎（不 fork / exec、不嵌入任何 web engine），而是把「網頁內容的
//     載入」表達為一個（模擬的）獨立行程 + 其可選的、能力閘控過的畫面 surface 橋接；內容
//     本身一律由呼叫端**注入**（`load(injected_content)`）——即本單元頭所稱的「注入式後端」。
//
// 語意映射（本單元的組裝邏輯）：
//   - `configure(config)`：消費一份 E7-01 宣告式 `Value`（Map，`url` 鍵，字串），設定目標
//     URL。無效輸入（非 Map / 缺 `url` / 非字串 / 空字串 / 不含 scheme 分隔符 `"://"`）→
//     `Invalid`，**不改動既有狀態**。若目前行程仍在跑（換 URL 前一律停止舊行程，不留跑著
//     卻與新 URL 不符的殘留行程）。成功 → `Ok`，清空既有內容、狀態轉 `Configured`。
//   - `load(injected_content)`：以呼叫端提供的內容「模擬」一次網頁載入（相位 1 無真實網路
//     擷取）。首次 load（或前次已停止）會經 `WidgetHost::start()` 啟動（模擬的）獨立行程；
//     是否嘗試橋接畫面 surface 一律先經 `surface_capable()`（NFR-03：呼叫前 `has()`／
//     `supported()` 閘控保護，從不盲目呼叫再處理失敗）——能力可用才會在 spec 帶上
//     `surface_id`。空注入內容視為無效載入（`Invalid`）。未先 `configure()` → `NotConfigured`。
//     成功時依是否橋接成功分為 `Loaded`（完整：含畫面 surface）或 `Degraded`（僅內容、無
//     surface——能力不可用或未注入能力後端時的降級路徑，NFR-03）。
//   - `reload()`：以上次 `configure()` 的 URL 重新啟動（模擬的）行程（`WidgetHost::restart()`，
//     沿用原本的橋接設定）；成功後內容被清空、狀態回到 `Configured`，等待呼叫端再次
//     `load()` 注入新內容（相位 1 無真實網路擷取，故 reload 與 load 是兩個明確分開的步驟）。
//     從未成功 load 過 → `NotConfigured`。
//   - `loading_state()`：目前具名載入狀態（NFR-02：具名，非布林裸值）。
//   - `render_model()`：以純文字模型呈現目前狀態（相位 1 無真實排版依賴，僅本單元自身
//     format）——狀態前綴 + URL（+ 已載入時附內容）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無真實瀏覽器引擎（不嵌入 WebView / CEF / 不發真實網路請求）、無絕對座標 /
// 數字 z-order（NFR-02）。任何無效操作一律明確回傳具名結果，不靜默。
//
// 依賴注入約定（皆不擁有其生命週期，須比本物件活得久）：
//   - `ds::kernel::KernelBackend&` / `ds::kernel::LayerStack&`：透傳給內部的 C1-01
//     `SkinProfile` 殼層（同 C1-01 的注入約定）。
//   - `ds::ipc::ProcessLauncher&` / `ds::ipc::MessageChannel&`：透傳給內部的 E10-05
//     `WidgetHost`（同 E10-05 的注入約定）。
//   - `ds::kernel::AlphaSurfaceService*`：選填的畫面 surface 能力後端（透傳給 `WidgetHost`）；
//     為 `nullptr` 時本 widget 一律走「無後端降級」路徑（不嘗試橋接，永遠 `Degraded`，但仍可
//     正常配置 / 載入 / 呈現內容）。
#ifndef DS_CONTENT_WIDGETS_C2_10_WEB_WIDGET_HPP
#define DS_CONTENT_WIDGETS_C2_10_WEB_WIDGET_HPP

#include <string>

#include "document.hpp"      // E7-01（上游，可讀不可改，經 c1_01 傳遞）：Value（configure() 消費）
#include "skin_profile.hpp"  // C1-01（上游，可讀不可改）：SkinProfile 桌面殼層基底
#include "widget_host.hpp"   // E10-05（上游，可讀不可改）：WidgetHost / WidgetSpec / ProcessLauncher /
                              //   MessageChannel（經其傳遞）/ AlphaSurfaceService（經其傳遞）

namespace ds::widgets {

// 網頁 widget 的具名載入狀態（NFR-02：具名，非布林裸值）。
enum class LoadingState {
    Idle,        // 尚未 configure()：無目標 URL。
    Configured,  // 已設定 URL（含 reload() 後等待重新 load() 的中繼態），尚無已載入內容。
    Loading,     // load() / reload() 執行中（相位 1 墊片單次求值，狀態機仍表達此中繼態）。
    Loaded,      // 已成功載入內容，且畫面 surface 已橋接（完整能力路徑）。
    Degraded,    // 已成功載入內容，但畫面 surface 未橋接（能力不可用 / 未注入能力後端，NFR-03 降級）。
    Failed,      // 上次 load() / reload() 失敗（無效注入內容 / 行程宿主啟動失敗）。
};

const char* to_string(LoadingState s) noexcept;

// `configure` / `load` / `reload` 的具名結果碼——不靜默。
enum class WebWidgetStatus {
    Ok,             // 操作成功。
    Invalid,        // 前置條件不滿足：設定非 Map / 缺或非法 url / 空注入內容 等。
    Unsupported,    // 已成功載入內容，但畫面 surface 能力不可用（降級為 `Degraded`，非硬錯誤）。
    NotConfigured,  // load() / reload() 於尚未（成功）configure() / load() 過的狀態下呼叫。
    HostError,      // E10-05 `WidgetHost` 啟動 / 重啟失敗（非能力閘控原因，如 launcher 拒絕）。
};

const char* to_string(WebWidgetStatus s) noexcept;

// 檢查一個候選字串是否為結構上合理的「網頁 URL」（相位 1 僅作最小結構檢查，不做完整
// RFC 3986 驗證）：非空，且含有 scheme 分隔符 `"://"`，且分隔符後仍有內容。
bool is_valid_web_url(const std::string& candidate);

// ---------------------------------------------------------------------------
// WebWidget —— 網頁 widget：組裝 C1-01（桌面殼層）+ E10-05（隔離行程 widget 宿主）。
//
// 每個實例代表**一個**具名桌面 widget（如 "widget.web"）。內部自持一個以注入後端 / 圖層堆疊
// 建構的 C1-01 `SkinProfile` 殼層，以及一個以注入 launcher / channel / 選填能力後端建構的
// E10-05 `WidgetHost`（代表「網頁內容」的隔離行程）。
// ---------------------------------------------------------------------------
class WebWidget {
public:
    // 建構一個具名 widget。id 即其殼層 surface 的具名 SurfaceId（NFR-02），並衍生出行程 /
    // 內容 surface 的具名識別碼（`<id>.proc` / `<id>.surface`）。後端 / 圖層堆疊 / launcher /
    // channel 皆為注入式相依（不取得所有權）；`surface_service` 選填，為 `nullptr` 時本
    // widget 一律走「無後端降級」路徑（見檔首）。
    WebWidget(std::string id, ds::kernel::KernelBackend& backend, ds::kernel::LayerStack& layers,
              ds::ipc::ProcessLauncher& launcher, ds::ipc::MessageChannel& channel,
              ds::kernel::AlphaSurfaceService* surface_service = nullptr);

    // 解構：若（模擬的）內容行程仍在跑，強制停止（`WidgetHost::stop()`），避免遺留執行中
    // 卻指向已銷毀本物件語意的行程 / 橋接 surface。
    ~WebWidget();

    WebWidget(const WebWidget&) = delete;
    WebWidget& operator=(const WebWidget&) = delete;

    // --- C1-01 桌面殼層（掛載基底）：圖層 / 定位 / 拖曳皆委派其自身 API。 ---
    ds::profiles::SkinProfile& shell() noexcept { return shell_; }
    const ds::profiles::SkinProfile& shell() const noexcept { return shell_; }

    const std::string& id() const noexcept { return shell_.id(); }

    // --- E10-05 隔離行程宿主（內容後端）：供進階呼叫端直接觀察行程 / 通道狀態。 ---
    ds::ipc::WidgetHost& host() noexcept { return host_; }
    const ds::ipc::WidgetHost& host() const noexcept { return host_; }

    // --- configure：設定目標 URL（見檔首語意映射）。 ---
    WebWidgetStatus configure(const ds::format::Value& config);

    // --- load：以注入內容模擬一次網頁載入（見檔首語意映射）。 ---
    WebWidgetStatus load(const std::string& injected_content);

    // --- reload：以上次設定的 URL 重新啟動內容行程（見檔首語意映射）。 ---
    WebWidgetStatus reload();

    // --- 查詢 ---
    LoadingState loading_state() const noexcept { return state_; }
    const std::string& url() const noexcept { return url_; }
    const std::string& content() const noexcept { return content_; }
    // 目前是否已成功橋接畫面 surface（`Loaded` 時為 true；`Degraded` / 其餘狀態為 false）。
    bool surface_bridged() const noexcept { return surface_bridged_; }
    // 能力閘控自檢（NFR-03）：是否已注入能力後端**且**該後端回報能力可用。是所有嘗試橋接
    // 動作的前置閘門——`load()` / `reload()` 一律先查詢本函式才決定是否請求橋接。
    bool surface_capable() const;

    // --- render_model：以純文字模型呈現目前狀態（見檔首語意映射）。 ---
    std::string render_model() const;

private:
    ds::ipc::WidgetSpec build_spec(bool with_surface) const;
    // 啟動（模擬的）內容行程；成功回 `Ok`（含橋接）或 `Unsupported`（成功但未橋接，降級）；
    // 其餘視為 `HostError`。副作用：更新 `surface_bridged_`。
    WebWidgetStatus start_host(bool with_surface);

    std::string proc_id() const { return id() + ".proc"; }
    std::string content_surface_id() const { return id() + ".surface"; }

    ds::profiles::SkinProfile shell_;               // C1-01 桌面殼層基底（注入後端 / 圖層堆疊）。
    ds::ipc::WidgetHost host_;                       // E10-05 隔離行程宿主（內容後端）。
    ds::kernel::AlphaSurfaceService* surface_service_;  // 選填能力後端（不擁有；透傳給 host_）。

    std::string url_;                                // 目前 / 最近一次 configure() 的目標 URL。
    std::string content_;                             // 目前已載入的內容（未載入 / 已卸載為空）。
    LoadingState state_ = LoadingState::Idle;
    bool surface_bridged_ = false;
};

}  // namespace ds::widgets

#endif  // DS_CONTENT_WIDGETS_C2_10_WEB_WIDGET_HPP
