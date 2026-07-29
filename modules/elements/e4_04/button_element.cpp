// E4-04 按鈕三態 — 實作（見 button_element.hpp 規格）。
#include "button_element.hpp"

#include <stdexcept>
#include <utility>

namespace ds::elements {

namespace {

// ButtonState → visuals_ 陣列索引；超出三個合法列舉值（如整數 static_cast 而來）回 -1
// （無效態，呼叫端須報 Invalid，不靜默套用 / 不靜默當作某個合法態）。
int state_index(ButtonState state) {
    switch (state) {
        case ButtonState::Normal:
            return 0;
        case ButtonState::Hover:
            return 1;
        case ButtonState::Pressed:
            return 2;
    }
    return -1;  // 不可達（合法列舉已窮舉）；非法列舉值的防禦
}

}  // namespace

ButtonElement::ButtonElement(ds::kernel::SurfaceId id, ds::kernel::Shape shape,
                             ds::kernel::SurfaceLayer layer)
    : id_(std::move(id)), shape_(std::move(shape)), layer_(layer) {
    if (id_.empty()) {
        throw std::invalid_argument("ButtonElement: id must not be empty");
    }
}

// ---------------------------------------------------------------------------
// 各態視覺（E4-02）
// ---------------------------------------------------------------------------

ButtonStatus ButtonElement::set_visual(ButtonState state, const ImageElement& image) {
    const int idx = state_index(state);
    if (idx < 0) {
        return ButtonStatus::Invalid;  // 無效態不套用
    }
    visuals_[static_cast<std::size_t>(idx)] = image;  // 值複製：獨立副本
    return ButtonStatus::Ok;
}

const ImageElement* ButtonElement::visual(ButtonState state) const {
    const int idx = state_index(state);
    if (idx < 0) {
        return nullptr;  // 無效態
    }
    return &visuals_[static_cast<std::size_t>(idx)];
}

// ---------------------------------------------------------------------------
// 懸停整合（E5-02）
// ---------------------------------------------------------------------------

ds::events::SubscriptionId ButtonElement::attach(ds::events::HoverTracker& tracker) {
    ds::kernel::HitSurface surface;
    surface.id = id_;
    surface.shape = shape_;
    surface.layer = layer_;
    surface.hit = ds::kernel::HitPolicy::Solid;
    tracker.add_surface(surface);
    return tracker.subscribe(
        [this](const ds::events::HoverEvent& event) { handle_hover_event(event); });
}

bool ButtonElement::detach(ds::events::HoverTracker& tracker,
                           ds::events::SubscriptionId subscription) {
    const bool unsubscribed = tracker.unsubscribe(subscription);
    const bool removed = tracker.remove_surface(id_);
    return unsubscribed && removed;
}

void ButtonElement::handle_hover_event(const ds::events::HoverEvent& event) noexcept {
    if (event.surface != id_) {
        return;  // 非本按鈕的事件：忽略（同一 tracker 可能同時追蹤多個按鈕）
    }
    switch (event.kind) {
        case ds::events::HoverEventKind::Enter:
            hover_ = true;
            break;
        case ds::events::HoverEventKind::Leave:
            hover_ = false;
            break;
        case ds::events::HoverEventKind::Move:
            break;  // 同一 surface 內移動：保持懸停中，不重發 Enter
    }
}

// ---------------------------------------------------------------------------
// 按下 / 放開（E1-04 命中）
// ---------------------------------------------------------------------------

ButtonStatus ButtonElement::press(const ds::kernel::LocalPoint& point) {
    const ds::kernel::HitResult result = tester_.hit_test(point, shape_);
    if (result.status == ds::kernel::HitStatus::Invalid) {
        return ButtonStatus::Invalid;  // 形狀 / 座標無效：報錯不靜默，不改變按下狀態
    }
    if (result.inside) {
        pressed_ = true;
    }
    // 未命中：按下動作落在按鈕外，非錯誤，單純不進入 Pressed。
    return ButtonStatus::Ok;
}

ButtonStatus ButtonElement::release(const ds::kernel::LocalPoint& point) {
    const ds::kernel::HitResult result = tester_.hit_test(point, shape_);
    if (result.status == ds::kernel::HitStatus::Invalid) {
        return ButtonStatus::Invalid;  // 形狀 / 座標無效：不改變任何狀態、不觸發 on_click
    }
    const bool was_pressed = pressed_;
    pressed_ = false;
    if (was_pressed && result.inside && on_click_) {
        on_click_();
    }
    return ButtonStatus::Ok;
}

// ---------------------------------------------------------------------------
// 狀態查詢 / 渲染描述
// ---------------------------------------------------------------------------

ButtonState ButtonElement::state() const noexcept {
    if (pressed_) {
        return ButtonState::Pressed;
    }
    if (hover_) {
        return ButtonState::Hover;
    }
    return ButtonState::Normal;
}

ButtonRenderModel ButtonElement::render_model() const {
    ButtonRenderModel model;
    model.state = state();
    const int idx = state_index(model.state);
    // idx 恆合法：model.state 僅可能為 state() 推導出的三個合法值之一。
    model.visual = visuals_[static_cast<std::size_t>(idx)].render_model();
    return model;
}

}  // namespace ds::elements
