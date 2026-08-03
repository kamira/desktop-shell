// W1-05 win32 自繪選單呈現 —— 把 E11-02 的渲染描述真的畫出來
//
// E11-02 `CustomMenuRenderer` 已經做完「模型 + 排版 + 相對幾何 + 巡覽」，
// 但它是平台中立的：它產出**描述**，不碰任何 OS。本單元補的只有兩件事：
//   1. 把那份描述用 GDI 畫進一個真實的彈出視窗
//   2. 把真實的滑鼠 / 鍵盤事件路由回 `hover()` / `select()` / `move_next()` …
//
// 銜接：托盤選單的模型是 E11-01 `TrayMenu`，而 E11-02 吃的是 E7-13 `Item` 森林，
// 故本單元提供轉換（`tray_menu_to_items`）。兩個模型都是既有契約，不新增第三個。
//
// ⚠ **與原生 HMENU 的取捨（必須清楚）**：
// 原生選單免費提供鍵盤巡覽、螢幕閱讀器無障礙、IME、DPI 縮放、邊緣翻轉。
// 自繪要自己重做。本單元做了鍵盤巡覽、邊緣翻轉與 DPI 感知的字型度量，
// **但沒有做無障礙（UI Automation / MSAA）**——螢幕閱讀器讀不到這個選單。
// 這是真實的功能退步，不是尚未測試的細節。詳見 CHG-20260803-15 的已知限制。
#ifndef DS_HOST_MENU_OWNER_DRAWN_MENU_HPP
#define DS_HOST_MENU_OWNER_DRAWN_MENU_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include <cstddef>
#include <string>
#include <vector>

#include "item_tree.hpp"     // E7-13（上游）：ds::format::Item
#include "menu_renderer.hpp"  // E11-02（上游）：CustomMenuRenderer / MenuRenderModel
#include "tray.hpp"           // E11-01（上游）：TrayMenu / TrayMenuItem

namespace ds::host {

// E11-01 TrayMenu → E7-13 Item 森林（E11-02 的輸入）。
//
// 每個項目的 `value` 是一個 Map，帶 E11-02 約定的具名鍵：
// `kind`（"action"/"separator"/"checkbox"/"submenu"）、`checked`、`enabled`。
// Item 的 `id` 取自命令 id；命令 id 為空者（分隔線、無命令項）以索引合成穩定 id，
// 因為 E11-02 要求 id 唯一，而 E11-01 允許空命令 id。
std::vector<ds::format::Item> tray_menu_to_items(const TrayMenu& menu);

// 選單配色。與 widget / 托盤圖示同一套，讓三者看起來是同一個產品。
struct MenuTheme {
    COLORREF background;
    COLORREF text;
    COLORREF text_disabled;
    COLORREF highlight;       // 目前巡覽列的底色
    COLORREF highlight_text;
    COLORREF separator;
    COLORREF check_mark;
};

MenuTheme default_menu_theme();

// 把 E11-02 的渲染描述畫到 hdc 上（只畫**根面板**，見檔頭的子選單限制）。
//
// 抽成自由函式而非私有方法，是為了可測試性：以記憶體 DIB 就能驗證
// 「高亮列真的有底色」「停用項用的是灰字」「分隔線畫在該畫的位置」，
// 不必先有彈出視窗。
// `model` 用來查每列的標籤文字：`MenuRowRender` 帶的是 E4-01 的排版結果（字形與行盒），
// 不是原始字串，故繪製時仍需回模型取 label。
void paint_menu_panel(HDC hdc, const RECT& bounds,
                      const MenuPanelRender& panel,
                      const MenuModel& model,
                      const MenuTheme& theme);

// 依索引路徑取節點；路徑無效回 nullptr。
const MenuNode* node_at(const MenuModel& model, const std::vector<std::size_t>& path);

// 依游標位置與面板尺寸決定彈出視窗的螢幕座標。
//
// 邊緣翻轉：右邊放不下就往左長，下面放不下就往上長——原生選單免費提供這個行為，
// 自繪必須自己做，否則選單會有一半在畫面外。純幾何，可獨立測試。
POINT place_menu_popup(POINT cursor, int panel_width, int panel_height,
                       const RECT& work_area);

// 自繪彈出選單：擁有一個真實視窗，內部持有 E11-02 renderer。
class OwnerDrawnMenu {
public:
    OwnerDrawnMenu();
    ~OwnerDrawnMenu();

    OwnerDrawnMenu(const OwnerDrawnMenu&) = delete;
    OwnerDrawnMenu& operator=(const OwnerDrawnMenu&) = delete;

    bool valid() const noexcept { return window_ != nullptr; }

    // 設定選單內容（由 E11-01 TrayMenu 轉入）。失敗回 false（模型不變）。
    bool set_menu(const TrayMenu& menu);

    // 於游標處彈出並跑一個小型 modal 迴圈直到使用者選取或取消。
    //   - 選到可致動項 → 回 true 並填入該項的索引路徑（交給 SystemTray::click 分派）
    //   - 取消（Esc / 點外面 / 失去焦點）→ 回 false，不觸碰 out_path
    bool popup_at(POINT cursor, std::vector<std::size_t>& out_path);

    // 目前渲染描述（供測試檢視排版結果）。
    MenuRenderModel render_model() const { return renderer_.render_model(); }
    const MenuModel& model() const noexcept { return renderer_.model(); }

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void paint();
    // 由視窗座標反查列索引路徑；未命中回 false。
    bool row_at(int y, std::vector<std::size_t>& out_path) const;

    ds::render::FixedFontMetrics metrics_{7.0, 20.0};
    CustomMenuRenderer renderer_;
    HWND window_ = nullptr;
    HFONT font_ = nullptr;
    bool done_ = false;
    bool accepted_ = false;
    std::vector<std::size_t> chosen_;
};

}  // namespace ds::host

#endif  // DS_HOST_MENU_OWNER_DRAWN_MENU_HPP
