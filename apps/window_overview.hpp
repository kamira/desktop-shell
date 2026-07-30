// apps/c4_04/window_overview.hpp — C4-04 視窗總覽（artifact 層 / apps，相位 1）
//
// 「視窗總覽」（Exposé 式視窗縮圖總覽）：按下召喚鍵時，把目前所有視窗以縮圖平鋪展示，
// 使用者可選取、切換（activate）或直接從總覽關閉某視窗。本單元不是新引擎邏輯，而是把
// 兩個已合併的擴充點/元件**組裝**成單一應用：
//
//   - C1-05（`ds::profiles::SummonPanelProfile`）：總覽面板本身的**基底 profile**——借用
//     它的短暫生命週期（叫出 = `open()`，收起 = `close()`）與召喚熱鍵組裝，本單元的
//     `show_overview()` / `close_overview()` 直接委派其 `open()` / `close()`。本單元**不**使用
//     C1-05 的 `items()` 階層式選項森林（那是給選單用的），視窗清單另以 `WindowEntry` 管理。
//   - E4-02（`ds::elements::ImageElement`）：每個視窗一個縮圖渲染描述——相位 1 縮圖為
//     **可注入的 `ImageSource`** 佔位圖像參照，無真實視窗截圖 / 無真實影像解碼。
//
// 行為組裝：`load_windows(清單)`（載入 / 取代目前視窗清單，並重新計算平鋪版位）、
// `show_overview(ttl)` / `close_overview()`（委派 C1-05 生命週期）、`select(window_id)`
// （總覽開啟中，標記目前反白選取的視窗，不關閉總覽）、`activate(window_id)`（選定並切換至
// 該視窗，隨即收起總覽）、`close_window(window_id)`（直接從總覽關閉某一視窗——自清單移除並
// 重新平鋪剩餘縮圖，與收起總覽本身是兩件事）。
//
// 相位 1（Mac / null 期）約束：純資料模型 + 注入式視窗來源（縮圖為佔位圖像參照，無真實
// 視窗列舉 / 無真實截圖），無平台分支（無 `#ifdef` / win32 / cocoa）。NFR-02（無絕對座標 /
// 無數字 z-order）：每個視窗縮圖的平鋪版位以**具名區域字串**（衍生自 `window_id`）+
// **正規化比例** [0,1]（占總覽畫布的分數，沿用 E4-02 `CropRect` 的精神）表達，不含任何螢幕
// 像素座標。任何無效操作（總覽未開啟即 select/activate、選取 / 啟用 / 關閉不存在的
// window_id、載入內含空 id 或重複 id 的清單）一律明確回傳具名結果，不靜默。
#ifndef DS_APPS_C4_04_WINDOW_OVERVIEW_HPP
#define DS_APPS_C4_04_WINDOW_OVERVIEW_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "image_element.hpp"          // E4-02（上游，可讀不可改）：ImageElement / ImageSource /
                                       //   ImageRenderModel
#include "summon_panel_profile.hpp"   // C1-05（上游，可讀不可改）：SummonPanelProfile（透傳
                                       //   E1-14 TransientProfileManager / E5-05 GlobalHotkeys /
                                       //   E1-02 InputStrategy / E7-13 Item 等）

namespace ds::apps {

// 總覽平鋪版位所在具名區域的字首（NFR-02：具名，非絕對座標）。
// 完整區域名 = kTileRegionPrefix + 該視窗的 window_id（如 "region.overview.tile.win.1"）。
inline constexpr const char* kTileRegionPrefix = "region.overview.tile.";

// 總覽平鋪格線的欄數上限（純版面配置參數，非螢幕座標；視窗數 < 欄數時，欄數收斂為視窗數）。
inline constexpr int kOverviewColumns = 4;

// 單一視窗縮圖於總覽平鋪中的版位：具名區域 + 正規化比例（NFR-02：具名 + normalized，
// 占總覽畫布的分數，非絕對像素座標；精神同 E4-02 CropRect）。
struct TileLayout {
    std::string region;   // 具名版位區域（NFR-02）。未平鋪（如清單為空）時為空字串。
    double x = 0.0;        // 平鋪格左上角於總覽畫布的正規化比例 [0,1]。
    double y = 0.0;
    double width = 0.0;    // 平鋪格占總覽畫布的正規化比例寬高 [0,1]。
    double height = 0.0;
};

// load_windows() 的具名結果。
enum class LoadStatus {
    Ok,       // 成功載入，取代既有視窗清單並重新平鋪。
    Invalid,  // 清單中有空 window_id 或重複 window_id——整批拒絕，既有清單不動（不部分套用）。
};

const char* to_string(LoadStatus s) noexcept;

// select() 的具名結果。
enum class SelectStatus {
    Selected,     // 成功選取（反白），總覽仍保持開啟。
    NotFound,     // 總覽開啟中，但清單內找不到該 window_id。
    PanelClosed,  // 總覽目前未開啟，操作被拒（不得對已收起的總覽選取）。
};

const char* to_string(SelectStatus s) noexcept;

// activate() 的具名結果。
enum class ActivateStatus {
    Activated,    // 成功切換至該視窗，總覽隨即收起。
    NotFound,     // 總覽開啟中，但清單內找不到該 window_id。
    PanelClosed,  // 總覽目前未開啟，操作被拒。
};

const char* to_string(ActivateStatus s) noexcept;

// ---------------------------------------------------------------------------
// WindowSpec —— load_windows() 的輸入：一個視窗的描述 + 可選縮圖來源。
//
// `thumbnail` 為可注入的 `ImageSource`（相位 1：佔位圖像參照，無真實截圖）；以指標傳遞、
// 不取得所有權——`load_windows()` 於呼叫當下把來源複製進內部 `ImageElement`（沿用 E4-02
// `set_source` 的值語意），呼叫端的來源物件載入後即可銷毀。`nullptr` = 該視窗無縮圖
// （仍是合法視窗，只是渲染描述 `has_source=false`，明確不假裝有資料）。
// ---------------------------------------------------------------------------
struct WindowSpec {
    std::string window_id;  // 具名識別（NFR-02：具名，非數字 handle）；不得為空，清單內不得重複。
    std::string title;
    const ds::elements::ImageSource* thumbnail = nullptr;
};

// 一個視窗於總覽中的完整條目：識別 + 標題 + 縮圖渲染描述（E4-02）+ 平鋪版位。
struct WindowEntry {
    std::string window_id;
    std::string title;
    ds::elements::ImageElement thumbnail;  // E4-02：縮圖渲染描述模型（佔位圖像參照）。
    TileLayout layout;
};

// ---------------------------------------------------------------------------
// WindowOverviewApp —— 視窗總覽應用：組裝 C1-05（總覽面板基底 profile 生命週期）+
// E4-02（每視窗縮圖）。
//
// 以參考持有上游 C1-05 `SummonPanelProfile`（不取得所有權，須比本物件活得久）——本物件
// 只借用其 open/close 生命週期，不碰其 items() 選單森林。
// ---------------------------------------------------------------------------
class WindowOverviewApp {
public:
    explicit WindowOverviewApp(ds::profiles::SummonPanelProfile& base);

    // --- 行為：load_windows（視窗清單 + E4-02 縮圖 + 平鋪版位）---

    // 以一份視窗清單取代現有清單：為每個視窗建立 E4-02 縮圖渲染描述（若提供 thumbnail 來源），
    // 並重新計算所有視窗的平鋪版位。
    //   - 清單內有空 window_id 或重複 window_id → `Invalid`，**現有清單不動**（不部分套用）。
    //   - 空清單（`specs` 為空）→ `Ok`，清單清空、無平鋪版位（合法的「無視窗」狀態）。
    //   - 目前有選取中的視窗，且該視窗不在新清單內 → 選取被清除（不留懸置選取）。
    LoadStatus load_windows(std::vector<WindowSpec> specs);

    const std::vector<WindowEntry>& windows() const noexcept { return windows_; }
    std::size_t window_count() const noexcept { return windows_.size(); }
    bool empty() const noexcept { return windows_.empty(); }

    // 依 window_id 尋找條目（唯讀）；找不到回 nullptr。
    const WindowEntry* find(const std::string& window_id) const noexcept;

    // --- 行為：show_overview / close_overview（委派 C1-05 生命週期）---

    // 叫出總覽：委派 `base().open(ttl)`。已開啟中 / ttl == 0 等 → false（委派 C1-05 語意）。
    bool show_overview(ds::events::Tick ttl);

    // 手動收起總覽：委派 `base().close()`。未開啟 → false（no-op）。清除目前選取。
    bool close_overview();

    bool is_open() const noexcept { return base_.is_open(); }
    ds::profiles::SummonPanelProfile& base() noexcept { return base_; }

    // --- 行為：select（總覽開啟中反白選取，不收起總覽）---

    SelectStatus select(const std::string& window_id);
    bool has_selection() const noexcept { return selected_index_ >= 0; }
    const WindowEntry* selected() const noexcept;
    void clear_selection() noexcept { selected_index_ = -1; }

    // --- 行為：activate（選定並切換視窗，隨即收起總覽）---

    // 找到該 window_id 並「切換」至該視窗：總覽隨即收起（委派 `base().close()`）、清除選取。
    //   - 總覽未開啟 → `PanelClosed`，`out` 不動。
    //   - 找不到 → `NotFound`，`out` 不動，總覽保持開啟（不得靜默收起）。
    //   - 找到 → `Activated`，`out`（若非 null）指向該條目，並更新 `last_activated()`。
    ActivateStatus activate(const std::string& window_id, const WindowEntry** out = nullptr);

    // 最近一次成功 activate() 的 window_id；從未成功過則為空字串。
    const std::string& last_activated() const noexcept { return last_activated_id_; }

    // --- 行為：close_window（直接從總覽關閉某一視窗，非收起總覽本身）---

    // 自清單移除該視窗並重新平鋪剩餘視窗。若該視窗恰為目前選取中的視窗，選取一併清除。
    //   - 找不到 window_id → false（no-op，不靜默）。
    bool close_window(const std::string& window_id);

private:
    void relayout();  // 依目前 windows_ 順序重新計算所有平鋪版位（NFR-02：具名 + normalized）。
    int index_of(const std::string& window_id) const noexcept;  // -1 = 找不到。

    ds::profiles::SummonPanelProfile& base_;

    std::vector<WindowEntry> windows_;
    int selected_index_ = -1;  // -1 = 無選取。
    std::string last_activated_id_;
};

}  // namespace ds::apps

#endif  // DS_APPS_C4_04_WINDOW_OVERVIEW_HPP
