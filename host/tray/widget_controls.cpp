// H1-02 widget 控制命令 + 托盤選單裝配 — 實作
#include "widget_controls.hpp"

namespace ds::host {

using ds::command::CommandArgs;
using ds::command::CommandResult;
using ds::kernel::InputPolicy;
using ds::kernel::SurfaceLayer;

void register_widget_controls(ds::command::CommandBus& bus,
                              ds::kernel::Win32KernelBackend& backend,
                              const ds::kernel::SurfaceId& surface_id,
                              WidgetControlState& state) {
    // Checkbox 命令會帶 "checked" 參數（E11-01 先切換勾選狀態再分派）。
    // 沒有該參數時退回「就地反轉」，讓命令也能被腳本 / 熱鍵直接叫用。
    bus.register_command(kCmdToggleTopmost, [&backend, &surface_id, &state](const CommandArgs& args) {
        state.topmost = args.get_bool("checked").value_or(!state.topmost);
        const bool ok = backend.set_surface_layer(
            surface_id, state.topmost ? SurfaceLayer::Topmost : SurfaceLayer::Normal);
        return ok ? CommandResult::make_ok()
                  : CommandResult::make_failed("set_surface_layer 失敗");
    });

    bus.register_command(kCmdTogglePassthrough,
                         [&backend, &surface_id, &state](const CommandArgs& args) {
        state.passthrough = args.get_bool("checked").value_or(!state.passthrough);
        const bool ok = backend.set_input_policy(
            surface_id, state.passthrough ? InputPolicy::PassThrough : InputPolicy::Accepting);
        return ok ? CommandResult::make_ok()
                  : CommandResult::make_failed("set_input_policy 失敗");
    });

    bus.register_command(kCmdQuit, [&state](const CommandArgs&) {
        state.quit = true;
        return CommandResult::make_ok();
    });
}

TrayMenu build_widget_tray_menu(const WidgetControlState& state) {
    TrayMenu menu;
    menu.items().push_back(
        TrayMenuItem::checkbox("最上層顯示", kCmdToggleTopmost, state.topmost));
    menu.items().push_back(
        TrayMenuItem::checkbox("點擊穿透", kCmdTogglePassthrough, state.passthrough));
    menu.items().push_back(TrayMenuItem::separator());
    menu.items().push_back(TrayMenuItem::action("結束", kCmdQuit));
    return menu;
}

}  // namespace ds::host
