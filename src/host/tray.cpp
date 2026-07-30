// E11-01 系統匣圖示與右鍵選單 — 實作
//
// 相位 1（Mac / null 期）：僅提供 null 後端工廠與選單點擊→E6-01 分派邏輯。此檔不含
// 任何平台分支或真實匣後端（無 NSStatusBar / Shell_NotifyIcon / #ifdef _WIN32）。
// 真實後端上線後在 make_default_tray 改派，呼叫端與選單邏輯不變。
#include "tray.hpp"

namespace ds::host {

TrayClickResult SystemTray::click(const std::vector<std::size_t>& path) {
    TrayClickResult out;

    TrayMenuItem* item = menu_.at_path(path);
    if (item == nullptr) {
        out.status = TrayClickStatus::InvalidPath;
        return out;
    }

    // 分隔線 / 子選單 / 停用項不可點擊：不分派、不變更狀態。
    if (!item->is_clickable()) {
        out.status = TrayClickStatus::NotClickable;
        return out;
    }

    // Checkbox：先切換勾選狀態、推送後端反映新狀態，再帶 "checked" 參數分派。
    if (item->kind() == TrayItemKind::Checkbox) {
        out.checked = item->toggle_checked();
        push_menu();  // 後端反映新的勾選狀態
    }

    // 可點擊項須綁命令 id 才可分派（純切換的勾選項可無命令）。
    if (!item->has_command()) {
        out.status = TrayClickStatus::NoCommand;
        return out;
    }

    // 未注入匯流排：無法分派（結構化回報，不崩潰）。
    if (bus_ == nullptr) {
        out.status = TrayClickStatus::NoBus;
        return out;
    }

    // 經 E6-01 命令匯流排分派。Checkbox 附帶新勾選狀態作為參數。
    ds::command::CommandArgs args;
    if (item->kind() == TrayItemKind::Checkbox) {
        args.set("checked", ds::command::CommandValue{out.checked});
    }
    out.command_result = bus_->dispatch(item->command_id(), args);
    out.status = TrayClickStatus::Dispatched;
    return out;
}

std::unique_ptr<SystemTray> make_default_tray(const ds::command::CommandBus* bus) {
    // 相位 1：平台中立 null 後端承接；記憶體記錄狀態，不觸碰任何真實系統匣。
    return std::make_unique<SystemTray>(std::make_unique<NullTrayBackend>(), bus);
}

}  // namespace ds::host
