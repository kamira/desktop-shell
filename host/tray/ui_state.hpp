// H1-04 widget UI 開關狀態的持久化
//
// 三個托盤開關（最上層 / 點擊穿透 / 鎖定位置）關掉再開要維持原狀態——
// 一個每次啟動都自己解鎖的「鎖定」等於沒有鎖。
//
// **不發明新格式**：沿用 E7-01 宣告式格式（`serialize` / `parse`），
// 與 E1-08 的位置檔（`positions.conf`）同一套。檔案長這樣：
//
//     format_version: 1.0
//     widget.controls:
//       topmost: true
//       passthrough: false
//       locked: false
//
// 與位置檔分開存的理由：位置檔的內容與格式由 **E1-08 擁有**
// （`serialize_positions` / `load_positions` 是它的 API）。UI 開關是 host 層的概念，
// 塞進去會讓 host 對一個上游單元的檔案格式產生隱性依賴。
#ifndef DS_HOST_TRAY_UI_STATE_HPP
#define DS_HOST_TRAY_UI_STATE_HPP

#include <string>

namespace ds::host {

// 三個 UI 開關的狀態。預設值與 host 建立 surface 時的 profile 一致。
struct WidgetUiState {
    bool topmost = true;       // SurfaceLayer::Topmost
    bool passthrough = false;  // InputPolicy::Accepting
    bool locked = false;       // 可拖曳（鎖定 = 停用拖曳）
};

// 序列化為 E7-01 宣告式文字（首行 format_version）。
std::string serialize_ui_state(const WidgetUiState& state);

// 從 E7-01 文字還原。**全有或全無**：任一欄位缺失 / 型別錯 / 文字無法解析 →
// 回 false 且**不觸碰 out**（不留半套狀態），與 E1-08 `load_positions` 的語意一致。
bool parse_ui_state(const std::string& text, WidgetUiState& out);

// 預設路徑 `%LOCALAPPDATA%\desktop-shell\ui-state.conf`；取不到 LOCALAPPDATA 回空字串。
std::string default_ui_state_path();

}  // namespace ds::host

#endif  // DS_HOST_TRAY_UI_STATE_HPP
