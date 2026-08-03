// W1-02 win32 系統匣後端 — 相位 2（Windows 期）
//
// 實作 E11-01 宣告的 `ds::host::TrayBackend` 抽象介面（上游，可讀不可改）。
// E11-01 的 `SystemTray` 已經處理好選單模型、勾選狀態切換、與 E6-01 命令分派；
// 本單元只補「真的長在 Windows 系統匣上」這一段：
//
//   TrayBackend::set_icon / set_tooltip → Shell_NotifyIcon(NIM_ADD / NIM_MODIFY)
//   TrayBackend::set_menu               → TrayMenu 模型遞迴轉成 HMENU
//   TrayBackend::show / hide            → 加入 / 移除匣圖示
//   使用者右鍵                          → TrackPopupMenu(TPM_RETURNCMD)
//
// **選單邏輯與命令分派一行都不重寫。** 使用者選取的項目以**索引路徑**回報，
// 由呼叫端交給 `SystemTray::click(path)`——那裡已經有 Checkbox 先切換再分派、
// 分隔線/子選單/停用項回 NotClickable、未綁命令回 NoCommand 等全部語意。
//
// 自建隱藏視窗，不借用 kernel 後端的 HWND：
//   系統匣回呼需要一個視窗收訊息。本後端自建一個 message-only 視窗，因此與 W1-01
//   完全解耦——托盤能獨立運作，kernel 後端也不必知道托盤存在。
//
// win32 專屬擴充（**不屬於 TrayBackend 契約**）：`poll_selection()`。
//   契約只有「推送選單給後端」這個方向，沒有「使用者選了什麼」的回呼——
//   相位 1 的 null 後端沒有真實使用者，不需要它。真實後端需要，故以非虛擬成員提供，
//   只有 win32 host 會呼叫。契約本身一個字未動。
#ifndef DS_HOST_WIN32_TRAY_WIN32_HPP
#define DS_HOST_WIN32_TRAY_WIN32_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstddef>
#include <string>
#include <vector>

#include "tray.hpp"  // E11-01（上游，可讀不可改）：TrayBackend / TrayMenu / TrayMenuItem

namespace ds::host {

// 一個可點擊選單項在 HMENU 中的命令 id ↔ 其在 TrayMenu 模型中的索引路徑。
struct TrayCommandBinding {
    UINT command_id = 0;                // TrackPopupMenu 回傳的值
    std::vector<std::size_t> path;      // 交給 SystemTray::click() 的索引路徑
};

// 由 TrayMenu 模型建出的原生選單 + 命令 id 對照表。
struct BuiltMenu {
    HMENU handle = nullptr;
    std::vector<TrayCommandBinding> bindings;
    std::vector<HMENU> submenus;  // 需一併釋放的子選單
};

// 把 TrayMenu 模型遞迴轉成原生 HMENU。
//
// 抽成自由函式而非私有方法，是為了**可測試性**：它不需要匣圖示、不需要使用者互動，
// 可以直接建出 HMENU 再用 GetMenuItemCount / GetMenuState 斷言結構與勾選狀態。
// 呼叫端負責以 destroy_built_menu() 釋放。
BuiltMenu build_native_menu(const TrayMenu& menu);
void destroy_built_menu(BuiltMenu& built);

class Win32TrayBackend final : public TrayBackend {
public:
    Win32TrayBackend();
    ~Win32TrayBackend() override;

    Win32TrayBackend(const Win32TrayBackend&) = delete;
    Win32TrayBackend& operator=(const Win32TrayBackend&) = delete;

    // --- TrayBackend 契約 ---
    // 真實系統匣。相位 1 的 null 後端回 false，本後端回 true——這正是能力閘控要區分的。
    bool has() const override { return message_window_ != nullptr; }
    void set_icon(const std::string& icon) override;
    void set_tooltip(const std::string& tooltip) override;
    void set_menu(const TrayMenu& menu) override;
    void show() override;
    void hide() override;

    // --- win32 專屬擴充（非契約）---
    // 取走使用者最近一次的選取。有選取回 true 並填入索引路徑；否則回 false。
    bool poll_selection(std::vector<std::size_t>& out_path);
    // 目前匣圖示是否已加入系統匣（供測試與診斷）。
    bool icon_added() const noexcept { return icon_added_; }
    // 目前圖示是否真的從 .ico 檔載入（false = 用了程式繪製的後備圖示）。
    // 供測試與診斷區分兩條路徑——否則「圖示看起來怪怪的」查不出是哪一種。
    bool icon_from_file() const noexcept { return icon_from_file_; }
    // 目前的原生圖示控制碼（供測試比對「後備圖示 ≠ 系統通用圖示」）。
    HICON native_icon() const noexcept { return icon_; }

    // W1-04：具名圖示 → 實際 .ico 檔路徑。
    //
    // 具名 id 去掉開頭的 `icon.` 之後即為套件內邏輯路徑，與 E9 套件格式的
    // `asset: icons/tray.png` 慣例一致：`icon.tray` → `<base>/icons/tray.ico`。
    //
    // base 目錄的解析順序：
    //   1. 環境變數 `DESKTOP_SHELL_ASSETS`（覆寫，供測試與自訂部署）
    //   2. `<執行檔所在目錄>/assets`
    // 回傳空字串代表無法決定路徑（例如 id 為空）。**不檢查檔案是否存在**——
    // 那是呼叫端的事，分開才能分別測試「路徑算對」與「檔案讀得到」。
    static std::wstring icon_path_for(const std::string& named_icon);

private:
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void on_tray_callback(WPARAM wp, LPARAM lp);
    void popup_menu_at_cursor();
    void refresh_icon_data(DWORD action);

    // 換掉目前圖示並管理其所有權。`owned` 為 true 者於替換 / 解構時須 DestroyIcon；
    // 系統共用圖示（LoadIcon(IDI_APPLICATION)）**不得**銷毀，故必須分開記。
    void adopt_icon(HICON icon, bool owned, bool from_file);
    // 程式繪製的後備圖示（找不到 .ico 檔時用）。失敗回 nullptr。
    static HICON draw_fallback_icon();

    HWND message_window_ = nullptr;
    NOTIFYICONDATAW nid_ = {};
    HICON icon_ = nullptr;
    bool icon_owned_ = false;
    bool icon_from_file_ = false;
    bool icon_added_ = false;
    std::string tooltip_;
    std::string icon_name_;
    TrayMenu menu_;
    BuiltMenu built_;
    std::vector<std::size_t> pending_selection_;
    bool has_pending_ = false;
};

}  // namespace ds::host

#endif  // DS_HOST_WIN32_TRAY_WIN32_HPP
