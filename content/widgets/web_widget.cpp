// content/widgets/c2_10/web_widget.cpp — C2-10 網頁 widget 實作
#include "web_widget.hpp"

#include <utility>

namespace ds::widgets {

namespace {
constexpr const char* kKeyUrl = "url";
constexpr const char* kProcEntry = "widget.web.render";
}  // namespace

const char* to_string(LoadingState s) noexcept {
    switch (s) {
        case LoadingState::Idle: return "idle";
        case LoadingState::Configured: return "configured";
        case LoadingState::Loading: return "loading";
        case LoadingState::Loaded: return "loaded";
        case LoadingState::Degraded: return "degraded";
        case LoadingState::Failed: return "failed";
    }
    return "unknown";
}

const char* to_string(WebWidgetStatus s) noexcept {
    switch (s) {
        case WebWidgetStatus::Ok: return "ok";
        case WebWidgetStatus::Invalid: return "invalid";
        case WebWidgetStatus::Unsupported: return "unsupported";
        case WebWidgetStatus::NotConfigured: return "not_configured";
        case WebWidgetStatus::HostError: return "host_error";
    }
    return "unknown";
}

bool is_valid_web_url(const std::string& candidate) {
    if (candidate.empty()) {
        return false;
    }
    const std::size_t pos = candidate.find("://");
    if (pos == std::string::npos || pos == 0) {
        return false;  // 須有 scheme（"://" 前不可為空）
    }
    return pos + 3 < candidate.size();  // "://" 後仍須有內容
}

WebWidget::WebWidget(std::string id, ds::kernel::KernelBackend& backend, ds::kernel::LayerStack& layers,
                      ds::ipc::ProcessLauncher& launcher, ds::ipc::MessageChannel& channel,
                      ds::kernel::AlphaSurfaceService* surface_service)
    : shell_(std::move(id), backend, layers),
      host_(launcher, channel, surface_service),
      surface_service_(surface_service) {}

WebWidget::~WebWidget() {
    if (host_.is_alive()) {
        host_.stop();  // 不遺留執行中卻指向已銷毀本物件語意的行程 / 橋接 surface。
    }
}

bool WebWidget::surface_capable() const {
    return surface_service_ != nullptr && surface_service_->supported();
}

ds::ipc::WidgetSpec WebWidget::build_spec(bool with_surface) const {
    ds::ipc::WidgetSpec spec;
    spec.id = proc_id();
    spec.entry = kProcEntry;
    spec.launch_args.set(kKeyUrl, url_);
    if (with_surface) {
        spec.surface_id = content_surface_id();
    }
    return spec;
}

WebWidgetStatus WebWidget::start_host(bool with_surface) {
    ds::ipc::WidgetSpec spec = build_spec(with_surface);
    const ds::ipc::WidgetHostStatus hs = host_.start(spec);
    if (hs == ds::ipc::WidgetHostStatus::Ok || hs == ds::ipc::WidgetHostStatus::AlreadyRunning) {
        surface_bridged_ = with_surface;
        return with_surface ? WebWidgetStatus::Ok : WebWidgetStatus::Unsupported;
    }
    // Invalid / NotRunning / SurfaceUnsupported 皆不應在此發生（`with_surface` 已先經
    // `surface_capable()` 閘控）；仍防禦性地一律回報 HostError，不崩潰、不留半殘留狀態。
    surface_bridged_ = false;
    return WebWidgetStatus::HostError;
}

WebWidgetStatus WebWidget::configure(const ds::format::Value& config) {
    if (!config.is_map()) {
        return WebWidgetStatus::Invalid;
    }
    const ds::format::Value* url_v = config.find(kKeyUrl);
    if (url_v == nullptr || !url_v->is_string() || !is_valid_web_url(url_v->as_string())) {
        return WebWidgetStatus::Invalid;  // 不改動既有狀態
    }
    if (host_.is_alive()) {
        host_.stop();  // 換 url 前一律停止舊行程，不留與新 url 不符的殘留行程。
    }
    url_ = url_v->as_string();
    content_.clear();
    surface_bridged_ = false;
    state_ = LoadingState::Configured;
    return WebWidgetStatus::Ok;
}

WebWidgetStatus WebWidget::load(const std::string& injected_content) {
    if (url_.empty()) {
        return WebWidgetStatus::NotConfigured;
    }
    if (injected_content.empty()) {
        state_ = LoadingState::Failed;
        return WebWidgetStatus::Invalid;  // 無效注入內容；不啟動 / 不變更行程狀態
    }
    state_ = LoadingState::Loading;

    if (!host_.is_alive()) {
        const WebWidgetStatus start_result = start_host(surface_capable());
        if (start_result == WebWidgetStatus::HostError) {
            state_ = LoadingState::Failed;
            return start_result;
        }
        // start_result 為 Ok（已橋接 surface）或 Unsupported（成功啟動但未橋接，降級路徑）。
    }

    content_ = injected_content;
    state_ = surface_bridged_ ? LoadingState::Loaded : LoadingState::Degraded;
    return surface_bridged_ ? WebWidgetStatus::Ok : WebWidgetStatus::Unsupported;
}

WebWidgetStatus WebWidget::reload() {
    if (url_.empty() || !host_.has_widget()) {
        return WebWidgetStatus::NotConfigured;  // 尚未設定 URL，或從未成功 load() 過
    }
    content_.clear();
    state_ = LoadingState::Loading;

    const ds::ipc::WidgetHostStatus hs = host_.restart();
    switch (hs) {
        case ds::ipc::WidgetHostStatus::Ok:
            // restart() 沿用上次 start() 的 spec（含 surface_id），橋接狀態不變。
            state_ = LoadingState::Configured;  // 行程已重啟，等待呼叫端 load() 注入新內容。
            return WebWidgetStatus::Ok;
        case ds::ipc::WidgetHostStatus::SurfaceUnsupported:
            surface_bridged_ = false;
            state_ = LoadingState::Configured;  // 行程重啟成功但未橋接 surface；仍等待 load()。
            return WebWidgetStatus::Unsupported;
        case ds::ipc::WidgetHostStatus::Invalid:
            state_ = LoadingState::Failed;
            surface_bridged_ = false;
            return WebWidgetStatus::Invalid;
        default:
            state_ = LoadingState::Failed;
            surface_bridged_ = false;
            return WebWidgetStatus::HostError;
    }
}

std::string WebWidget::render_model() const {
    std::string out = "[";
    out += to_string(state_);
    out += "] ";
    switch (state_) {
        case LoadingState::Idle:
            out += "(no url configured)";
            break;
        case LoadingState::Configured:
        case LoadingState::Loading:
        case LoadingState::Failed:
            out += url_.empty() ? std::string("(no url configured)") : url_;
            break;
        case LoadingState::Loaded:
        case LoadingState::Degraded:
            out += url_;
            out += "\n";
            out += content_;
            break;
    }
    return out;
}

}  // namespace ds::widgets
