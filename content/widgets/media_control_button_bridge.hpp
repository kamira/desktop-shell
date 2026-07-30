// content/widgets/c2_07/media_control_button_bridge.hpp — C2-07 內部橋接層：E4-04 控制按鈕
//
// 為何需要一個「橋接層」而不直接在 `media_control_widget.hpp` 使用 `ds::elements::ButtonElement`：
// 本單元須在同一個 `MediaControlWidget`（`media_control_widget.hpp`）內同時組裝 C1-01（其
// `skin_profile.hpp` #include `input_strategy.hpp`，於 `ds::kernel` 宣告 `enum class
// HitResult`）與 E4-04（`button_element.hpp` #include `hover_tracker.hpp` → `hit_test.hpp`，於
// 同一 `ds::kernel` 命名空間宣告**另一個不同型別**的 `struct HitResult`）。這兩個上游擴充點
// （E1-02 / E1-04）各自獨立、各自已合併、彼此互不相依，但**兩者的標頭若在同一翻譯單元內同時
// `#include`，會因 `ds::kernel::HitResult` 同名不同型別（enum class 對 struct）而編譯失敗**
// （已於本機以 g++ 實測重現：`use of 'HitResult' with tag type that does not match previous
// declaration`）。這是上游既有、本單元 write_scope 之外的命名碰撞，不可修改上游解決。
//
// 此碰撞已由已合併的 C1-06（`content/profiles/c1_06/dock_hot_zone_bridge.*`，同樣需同時組裝
// E1-02 與另一支 transitively 帶入 E1-04 的擴充點）率先發現並確立解法：把「實際觸碰 E1-04 的
// 那一半」隔離到獨立翻譯單元——橋接層的**標頭**只使用中立值型別（不 include 任何 E1-04 相關
// 標頭），只有其 **.cpp**（且僅有它）才 `#include "button_element.hpp"`。本檔即依此既有範式
// 為 C2-07 提供的橋接層：`media_control_widget.hpp` 因此可以安全地同時 `#include
// "skin_profile.hpp"`（E1-02）與本檔（不透明橋接），兩者共存於同一翻譯單元不會觸發上述碰撞。
//
// `ds::elements::ImageRenderModel`（E4-02 `image_element.hpp`）不在此碰撞範圍內——E4-02 僅
// `#include "alpha_surface.hpp"`（E1-03），並未觸及 E1-04 / E1-02，故可直接在本橋接層的公開
// 介面中使用，不需額外中立化。
#ifndef DS_CONTENT_WIDGETS_C2_07_MEDIA_CONTROL_BUTTON_BRIDGE_HPP
#define DS_CONTENT_WIDGETS_C2_07_MEDIA_CONTROL_BUTTON_BRIDGE_HPP

#include <functional>
#include <memory>
#include <string>

#include "image_element.hpp"  // E4-02（上游，可讀不可改；不觸及 E1-04，安全直接使用）：ImageRenderModel

namespace ds::widgets {

// 按鈕三態——鏡射 E4-04 `ds::elements::ButtonState` 的具名集合（值一一對應，橋接層負責轉換）。
// 維持獨立型別是隔離上述命名碰撞的必要手段，非重複造輪子。
enum class MediaButtonState {
    Normal,
    Hover,
    Pressed,
};

// 鏡射 E4-04 `ButtonRenderModel`——目前顯示態 + 該態的 E4-02 渲染描述。
struct MediaButtonRenderModel {
    MediaButtonState state = MediaButtonState::Normal;
    ds::elements::ImageRenderModel visual;
};

// 點擊回呼——於 `click()` 觸發（模擬一次按下後於按鈕範圍內放開）時呼叫一次。
using MediaButtonClickCallback = std::function<void()>;

// ---------------------------------------------------------------------------
// MediaControlButtonBridge —— 單一具名控制按鈕的不透明橋接（pimpl，委派 E4-04
// `ButtonElement`）。本標頭不 include 任何 E1-04 相關標頭；實際串接於 `.cpp`。
// ---------------------------------------------------------------------------
class MediaControlButtonBridge {
public:
    // 建構一個具名按鈕（id 即其 E4-04 `SurfaceId`）；內部以單位矩形（[0,1]x[0,1] 本地座標）
    // 作為命中形狀，配合 `click()` 於中心點模擬一次 press+release。
    explicit MediaControlButtonBridge(std::string id);
    ~MediaControlButtonBridge();

    MediaControlButtonBridge(MediaControlButtonBridge&&) noexcept;
    MediaControlButtonBridge& operator=(MediaControlButtonBridge&&) noexcept;
    MediaControlButtonBridge(const MediaControlButtonBridge&) = delete;
    MediaControlButtonBridge& operator=(const MediaControlButtonBridge&) = delete;

    // 設定點擊回呼（委派 E4-04 `set_on_click`）。
    void set_on_click(MediaButtonClickCallback callback);

    // 模擬一次點擊：於按鈕中心點依序 press → release（命中，觸發 on_click）。
    void click();

    // 目前渲染描述（委派 E4-04 `render_model()`，經轉換為中立型別）。
    MediaButtonRenderModel render_model() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ds::widgets

#endif  // DS_CONTENT_WIDGETS_C2_07_MEDIA_CONTROL_BUTTON_BRIDGE_HPP
