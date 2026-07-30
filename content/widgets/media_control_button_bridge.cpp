// content/widgets/c2_07/media_control_button_bridge.cpp — MediaControlButtonBridge 實作
//
// 本檔是本單元**唯一** `#include "button_element.hpp"`（E4-04，transitively 帶入 E1-04
// `hit_test.hpp`）之處——理由見 `media_control_button_bridge.hpp` 標頭說明：藉由把實際的
// 上游串接隔離到獨立翻譯單元，避免 E1-02 `input_strategy.hpp`（經 `skin_profile.hpp` 由
// `media_control_widget.hpp` 引入）與 E4-04 `button_element.hpp` 在同一翻譯單元內因
// `ds::kernel::HitResult` 同名不同型別（enum class 對 struct）而編譯失敗。
#include "media_control_button_bridge.hpp"

#include <utility>

#include "button_element.hpp"  // E4-04（上游，可讀不可改）：ButtonElement / ButtonRenderModel / ButtonState；
                                // 並透過其 include 傳遞 E1-04 LocalPoint / Shape / make_rect。

namespace ds::widgets {

namespace {

MediaButtonState from_kernel_state(ds::elements::ButtonState s) {
    switch (s) {
        case ds::elements::ButtonState::Normal:
            return MediaButtonState::Normal;
        case ds::elements::ButtonState::Hover:
            return MediaButtonState::Hover;
        case ds::elements::ButtonState::Pressed:
            return MediaButtonState::Pressed;
    }
    return MediaButtonState::Normal;  // 不可達（E4-04 enum 已窮舉）。
}

// 中央命中點（單位矩形 [0,1]x[0,1] 內），供 press/release 模擬一次點擊。
constexpr ds::kernel::LocalPoint kButtonCenter{0.5f, 0.5f};

}  // namespace

struct MediaControlButtonBridge::Impl {
    ds::elements::ButtonElement button;
    explicit Impl(std::string id) : button(std::move(id), ds::kernel::make_rect(1.0f, 1.0f)) {}
};

MediaControlButtonBridge::MediaControlButtonBridge(std::string id)
    : impl_(std::make_unique<Impl>(std::move(id))) {}

MediaControlButtonBridge::~MediaControlButtonBridge() = default;
MediaControlButtonBridge::MediaControlButtonBridge(MediaControlButtonBridge&&) noexcept = default;
MediaControlButtonBridge& MediaControlButtonBridge::operator=(MediaControlButtonBridge&&) noexcept =
    default;

void MediaControlButtonBridge::set_on_click(MediaButtonClickCallback callback) {
    impl_->button.set_on_click(std::move(callback));
}

void MediaControlButtonBridge::click() {
    impl_->button.press(kButtonCenter);
    impl_->button.release(kButtonCenter);  // 命中中心點放開 → 觸發 on_click。
}

MediaButtonRenderModel MediaControlButtonBridge::render_model() const {
    const ds::elements::ButtonRenderModel m = impl_->button.render_model();
    return MediaButtonRenderModel{from_kernel_state(m.state), m.visual};
}

}  // namespace ds::widgets
