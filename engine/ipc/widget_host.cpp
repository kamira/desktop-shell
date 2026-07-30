// E10-05 獨立行程 widget 宿主 — 實作
//
// 相位 1：`NullProcessLauncher` 純記憶體模擬（無 fork/exec/win32 CreateProcess 等真實行程
// API）；`WidgetHost` 的生命週期 / 崩潰偵測 / surface 橋接邏輯與行程實作方式無關，
// 相位 2 換上真實 `ProcessLauncher` 後端時本檔不需改動。
#include "widget_host.hpp"

#include <utility>

namespace ds::ipc {

const char* widget_host_contract_version() noexcept { return "e10_05/1.0.0"; }

// ---------------------------------------------------------------------------
// NullProcessLauncher
// ---------------------------------------------------------------------------

ProcessHandle NullProcessLauncher::spawn(const WidgetSpec& spec) {
    // 明確拒絕無法識別的 spec：不留半份狀態、不建立任何記錄。
    if (spec.id.empty()) {
        return ProcessHandle{};
    }
    ProcessHandle handle = "proc." + spec.id + "#" + std::to_string(next_id_++);
    processes_[handle] = Entry{/*alive=*/true};
    return handle;
}

bool NullProcessLauncher::terminate(const ProcessHandle& handle) {
    auto it = processes_.find(handle);
    if (it == processes_.end() || !it->second.alive) {
        return false;  // 未知 / 已不存活：不崩潰，回 false
    }
    it->second.alive = false;
    return true;
}

bool NullProcessLauncher::is_alive(const ProcessHandle& handle) const {
    auto it = processes_.find(handle);
    return it != processes_.end() && it->second.alive;
}

bool NullProcessLauncher::simulate_crash(const ProcessHandle& handle) {
    auto it = processes_.find(handle);
    if (it == processes_.end() || !it->second.alive) {
        return false;
    }
    it->second.alive = false;
    return true;
}

// ---------------------------------------------------------------------------
// WidgetHost
// ---------------------------------------------------------------------------

namespace {

const char* alpha_mode_name(ds::kernel::AlphaMode mode) {
    switch (mode) {
        case ds::kernel::AlphaMode::Opaque:
            return "opaque";
        case ds::kernel::AlphaMode::PerPixel:
            return "per_pixel";
    }
    return "opaque";
}

}  // namespace

void WidgetHost::publish_lifecycle(const MessageType& type) const {
    ds::command::CommandArgs args;
    args.set("widget_id", spec_.id).set("process_handle", handle_);
    channel_.publish(Message{type, std::move(args)});
}

void WidgetHost::publish_surface_event(const MessageType& type) const {
    ds::command::CommandArgs args;
    args.set("widget_id", spec_.id).set("surface_id", spec_.surface_id);
    if (type == kWidgetSurfaceAttachedType) {
        args.set("mode", std::string(alpha_mode_name(spec_.surface_alpha.mode)))
            .set("opacity", static_cast<double>(spec_.surface_alpha.opacity));
    }
    channel_.publish(Message{type, std::move(args)});
}

void WidgetHost::teardown_running() {
    launcher_.terminate(handle_);
    if (surface_attached_ && surface_service_ != nullptr) {
        surface_service_->destroy_alpha_surface(spec_.surface_id);
        surface_attached_ = false;
        publish_surface_event(kWidgetSurfaceDetachedType);
    }
}

WidgetHostStatus WidgetHost::start(WidgetSpec spec) {
    if (spec.id.empty() || spec.entry.empty()) {
        return WidgetHostStatus::Invalid;  // 無效 spec：明確回報，不啟動任何行程
    }
    if (state_ == ProcessState::Running) {
        return WidgetHostStatus::AlreadyRunning;
    }

    const bool wants_surface = !spec.surface_id.empty();
    if (wants_surface) {
        // Surface 橋接前置檢查：先確認能力可用，避免啟動一個之後得回滾的行程。
        if (surface_service_ == nullptr) {
            return WidgetHostStatus::Invalid;  // 要求橋接但未注入服務：明確拒絕
        }
        if (!surface_service_->supported()) {
            return WidgetHostStatus::SurfaceUnsupported;  // NFR-03：能力不可用，明確回報
        }
        if (surface_service_->has_alpha_surface(spec.surface_id)) {
            return WidgetHostStatus::Invalid;  // surface id 衝突
        }
    }

    ProcessHandle handle = launcher_.spawn(spec);
    if (handle.empty()) {
        return WidgetHostStatus::Invalid;  // launcher 拒絕（其層自身判定 spec 無效）
    }

    if (wants_surface) {
        ds::kernel::SurfaceProfile profile{};  // 預設四參數 profile；本宿主僅代管 alpha 狀態
        ds::kernel::AlphaStatus st =
            surface_service_->create_alpha_surface(spec.surface_id, profile, spec.surface_alpha);
        if (st != ds::kernel::AlphaStatus::Ok) {
            launcher_.terminate(handle);  // 回滾：不留孤兒行程
            return st == ds::kernel::AlphaStatus::Unsupported
                       ? WidgetHostStatus::SurfaceUnsupported
                       : WidgetHostStatus::Invalid;
        }
    }

    handle_ = std::move(handle);
    spec_ = std::move(spec);
    has_spec_ = true;
    surface_attached_ = wants_surface;
    state_ = ProcessState::Running;

    publish_lifecycle(kWidgetStartedType);
    if (surface_attached_) {
        publish_surface_event(kWidgetSurfaceAttachedType);
    }
    return WidgetHostStatus::Ok;
}

WidgetHostStatus WidgetHost::stop() {
    if (state_ != ProcessState::Running) {
        return WidgetHostStatus::NotRunning;  // 明確回報，不崩潰
    }
    teardown_running();
    state_ = ProcessState::Stopped;
    publish_lifecycle(kWidgetStoppedType);
    return WidgetHostStatus::Ok;
}

WidgetHostStatus WidgetHost::restart() {
    if (!has_spec_) {
        return WidgetHostStatus::Invalid;  // 未曾啟動過，無 spec 可重啟
    }
    WidgetSpec spec = spec_;  // 複製：teardown_running() / start() 會動 spec_ / handle_
    if (state_ == ProcessState::Running || state_ == ProcessState::Crashed) {
        teardown_running();
        state_ = ProcessState::Stopped;
    }
    return start(std::move(spec));
}

bool WidgetHost::is_alive() const {
    return state_ == ProcessState::Running && launcher_.is_alive(handle_);
}

bool WidgetHost::poll_crash() {
    if (state_ != ProcessState::Running) {
        return false;  // 非執行中，無崩潰可偵測
    }
    if (launcher_.is_alive(handle_)) {
        return false;  // 仍存活，非崩潰
    }
    state_ = ProcessState::Crashed;
    publish_lifecycle(kWidgetCrashedType);
    if (on_crash_) {
        on_crash_(spec_.id);
    }
    return true;
}

}  // namespace ds::ipc
