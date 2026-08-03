// H1-04 widget UI 開關狀態的持久化 — 實作
#include "ui_state.hpp"

#include <cstdlib>

#include "document.hpp"   // E7-01（上游）：Value / parse / kSupportedFormat
#include "writeback.hpp"  // E7-12（上游）：set_value / serialize

namespace ds::host {
namespace {

constexpr const char* kSection = "widget.controls";
constexpr const char* kTopmost = "topmost";
constexpr const char* kPassthrough = "passthrough";
constexpr const char* kLocked = "locked";

// 以**顯式 Path 段**寫入，避免含 '.' 的鍵（"widget.controls"）被字串路徑的 '.' 誤拆——
// 與 E1-08 `serialize_positions` 處理具名 SurfaceId 的理由相同。
ds::format::Path field(const char* name) {
    using ds::format::PathSegment;
    return {PathSegment::of_key(kSection), PathSegment::of_key(name)};
}

// 讀取一個必填的布林欄位。缺欄位 / 型別不符 → false（呼叫端據此整批放棄）。
bool read_bool(const ds::format::Value& section, const char* name, bool& out) {
    const ds::format::Value* v = section.find(name);
    if (v == nullptr || !v->is_bool()) return false;
    out = v->as_bool();
    return true;
}

}  // namespace

std::string serialize_ui_state(const WidgetUiState& state) {
    using ds::format::set_value;
    using ds::format::Value;

    Value root = Value::map({});
    root = set_value(root, field(kTopmost), Value::boolean(state.topmost));
    root = set_value(root, field(kPassthrough), Value::boolean(state.passthrough));
    root = set_value(root, field(kLocked), Value::boolean(state.locked));
    return ds::format::serialize(root, ds::format::kSupportedFormat);
}

bool parse_ui_state(const std::string& text, WidgetUiState& out) {
    const ds::format::ParseResult result = ds::format::parse(text);
    if (!result.ok()) return false;

    const ds::format::Value* section = result.document().root.find(kSection);
    if (section == nullptr || !section->is_map()) return false;

    // 全有或全無：先全部讀進暫存，三個欄位都成立才寫回 out。
    WidgetUiState parsed;
    if (!read_bool(*section, kTopmost, parsed.topmost)) return false;
    if (!read_bool(*section, kPassthrough, parsed.passthrough)) return false;
    if (!read_bool(*section, kLocked, parsed.locked)) return false;

    out = parsed;
    return true;
}

std::string default_ui_state_path() {
    const char* base = std::getenv("LOCALAPPDATA");
    if (base == nullptr || *base == '\0') return std::string();
    return std::string(base) + "\\desktop-shell\\ui-state.conf";
}

}  // namespace ds::host
