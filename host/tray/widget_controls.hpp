// H1-02 widget 控制命令 + 托盤選單裝配
//
// 把「托盤選單長什麼樣」與「每一項要做什麼」集中在一處，並且**做成可測的**。
//
// 為什麼要抽出來而不是寫在 wWinMain 裡：托盤最關鍵的一段是
// 「使用者選了某項 → E6-01 命令 → 後端狀態真的改變」這條鏈。
// 寫在 wWinMain 裡就只能靠人工點擊驗證；抽出來之後，測試可以直接呼叫
// `SystemTray::click(path)` 走完整條鏈，只留「滑鼠實際點下去」那一步給操作驗收。
//
// 此處沒有任何選單邏輯：勾選切換、分隔線 / 停用項的處理、命令分派全部由 E11-01
// `SystemTray` 負責；W1-02 後端只回報使用者選了哪一項的索引路徑。
#ifndef DS_HOST_TRAY_WIDGET_CONTROLS_HPP
#define DS_HOST_TRAY_WIDGET_CONTROLS_HPP

#include "command_bus.hpp"    // E6-01（上游）
#include "tray.hpp"           // E11-01（上游）：TrayMenu / TrayMenuItem
#include "win32_backend.hpp"  // W1-01：Win32KernelBackend

namespace ds::host {

// 控制項的目前狀態。與建立 surface 時的 profile 一致，由命令處理器更新。
struct WidgetControlState {
    bool topmost = true;       // 對應 SurfaceLayer::Topmost
    bool passthrough = false;  // 對應 InputPolicy::Accepting
    bool locked = false;       // 鎖定位置 = 停用拖曳（W1-03 set_draggable 的反面）
    bool quit = false;         // 使用者選了「結束」
};

// 具名命令 id —— 選單項與處理器之間唯一的耦合點。
inline constexpr const char* kCmdToggleTopmost = "widget.toggle_topmost";
inline constexpr const char* kCmdTogglePassthrough = "widget.toggle_passthrough";
inline constexpr const char* kCmdToggleLock = "widget.toggle_lock";
inline constexpr const char* kCmdQuit = "app.quit";

// 把三個 widget 控制命令註冊到匯流排。處理器捕獲 `backend` / `surface_id` / `state` 的參照，
// 三者必須比匯流排活得久。已註冊過同 id 則該次註冊回 false（E6-01 語意），本函式不覆寫。
void register_widget_controls(ds::command::CommandBus& bus,
                              ds::kernel::Win32KernelBackend& backend,
                              const ds::kernel::SurfaceId& surface_id,
                              WidgetControlState& state);

// 依目前狀態組出托盤選單（勾選狀態反映 `state`）。
TrayMenu build_widget_tray_menu(const WidgetControlState& state);

}  // namespace ds::host

#endif  // DS_HOST_TRAY_WIDGET_CONTROLS_HPP
