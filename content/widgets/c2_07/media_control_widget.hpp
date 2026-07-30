// content/widgets/c2_07/media_control_widget.hpp — C2-07 媒體控制 widget（artifact 層 / 相位 1：組裝型 widget）
//
// 語意：桌面「媒體控制」widget —— 經 **E2-13** 注入式 `MetricRegistry` 讀取「media.nowplaying」
// 指標（曲名 / 演出者 / 專輯 / 播放狀態 / 封面參照），以 **E4-02** `ImageElement` 把封面參照組裝
// 為圖片渲染描述，並以 **E4-04** `ButtonElement`（播放 / 暫停 / 上一首 / 下一首四顆具名控制
// 按鈕，經本單元的 `MediaControlButtonBridge` 橋接——原因見下方「上游命名碰撞」段落）作為控制
// 元素，其點擊直接觸發 **E3-03** `MediaControlActuator`（注入式後端，相位 1 為
// `NullMediaControlBackend`）的對應命令處理器（`handle_play` / `handle_pause` / `handle_next` /
// `handle_prev`）。掛 **C1-01**（`SkinProfile`）為桌面基底（具名圖層歸屬 / 輸入策略 / 透明外形 /
// 自由拖曳皆透傳，本單元不重造）。
//
// 上游命名碰撞（發現於本單元施工過程，已依既有範式解決）：C1-01（`skin_profile.hpp` #include
// `input_strategy.hpp`，E1-02 於 `ds::kernel` 宣告 `enum class HitResult`）與 E4-04
// （`button_element.hpp` #include `hover_tracker.hpp` → `hit_test.hpp`，E1-04 於同一
// `ds::kernel` 命名空間宣告**另一個不同型別**的 `struct HitResult`）若同一翻譯單元內同時
// `#include`，會編譯失敗（`use of 'HitResult' with tag type that does not match previous
// declaration`）。此碰撞已由已合併的 C1-06（`content/profiles/c1_06/dock_hot_zone_bridge.*`）
// 率先發現並確立解法：把「實際觸碰 E1-04 的那一半」隔離到獨立翻譯單元。本單元依此範式提供
// `media_control_button_bridge.hpp`/`.cpp`（本檔 write_scope 內，未動任何上游檔案）——本標頭
// 因此只 `#include` 該橋接層的中立標頭，不直接 `#include "button_element.hpp"`。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實媒體 API（指標來源與控制後端皆為
// 注入式，相位 1 分別為記憶體內指標與 `NullMediaControlBackend`）、無真實影像解碼、無平台分支
// （無 `#ifdef` / win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02，全數透傳上游具名模型）。
// 任何無效操作一律回傳具名結果，不靜默：
//   - **無媒體降級**：注入的 `MetricRegistry` 為 nullptr、尚未掛上 "media.nowplaying" 指標、或
//     該指標的 "media.title" 欄位實例 `valid==false`（與 E2-13 "無播放" 語意一致——`has_media`
//     欄位群一律同步變動），widget 走降級路徑（`has_media()==false`、文字欄位清空、封面
//     `has_source=false`），回 `Ok`（非錯誤，這是誠實的「目前無播放」狀態）。
//   - **結構性錯誤**：指標**存在**但缺少任一必要欄位實例（state / title / artist / album /
//     artwork）→ `Invalid`，不套用、既有 `render_model()` 保留。
//
// 能力閘控（NFR-03）：`has_control()` 於 `play()` / `pause()` / `next()` / `prev()` 前一律
// `has()` 式保護——未注入 actuator（或其後端為 null）時回 `MediaControlStatus::Unavailable`，
// 不觸發任何按鈕、不呼叫後端、不崩潰。
//
// 依賴注入約定（皆不擁有其生命週期，須比本物件活得久）：
//   - `ds::kernel::KernelBackend&` / `ds::kernel::LayerStack&`：透傳給內部的 C1-01 `SkinProfile`
//     基底（同 C1-01 約定）。
//   - `std::shared_ptr<ds::metrics::MetricRegistry>`：相位 1 由呼叫端以 E2-13
//     `MediaMetadataProvider::register_metrics` 掛好 "media.nowplaying" 指標後注入；可為
//     nullptr（`refresh()` 保守降級為「無媒體」，不崩）。
//   - `std::shared_ptr<ds::actuators::MediaControlActuator>`：相位 1 為預設綁
//     `NullMediaControlBackend` 的致動器；可為 nullptr（控制方法能力閘控回 `Unavailable`）。
//
// 命名空間 `ds::widgets`。
#ifndef DS_CONTENT_WIDGETS_C2_07_MEDIA_CONTROL_WIDGET_HPP
#define DS_CONTENT_WIDGETS_C2_07_MEDIA_CONTROL_WIDGET_HPP

#include <memory>
#include <string>

#include "image_element.hpp"              // E4-02（上游，可讀不可改）：ImageElement / ImageRenderModel /
                                           //   MemoryImageSource / ImageDimensions
#include "media_control_actuator.hpp"     // E3-03（上游，可讀不可改）：MediaControlActuator /
                                           //   MediaControlBackend / NullMediaControlBackend /
                                           //   MediaControlState；並透過其 include 傳遞 E6-01 CommandArgs。
#include "media_control_button_bridge.hpp"  // C2-07 內部橋接層（本檔 write_scope）：中立化 E4-04
                                             //   ButtonElement，隔離其與 C1-01 transitively 帶入的
                                             //   E1-02 之間的上游 ds::kernel::HitResult 命名碰撞
                                             //   （見上方標頭說明）。
#include "media_metadata.hpp"             // E2-13（上游，可讀不可改）：MediaMetadataProvider（欄位鍵常數）/
                                           //   PlaybackState / to_string
#include "metric.hpp"                     // E2-01（經 E2-13 上游傳遞，可讀不可改）：MetricRegistry / Metric /
                                           //   MetricInstance / MetricValue
#include "skin_profile.hpp"               // C1-01（上游，可讀不可改）：SkinProfile（桌面基底）

namespace ds::widgets {

// 媒體控制 widget 操作的具名結果（與上游各單元同精神：明確、不靜默）。
enum class MediaControlStatus {
    Ok,           // 操作成功（含「目前無播放」的降級顯示——非錯誤）。
    Invalid,      // 結構性錯誤：注入的指標存在但缺少必要欄位實例，不套用、既有狀態不變。
    Unavailable,  // 能力閘控（NFR-03）：控制方法呼叫時未注入 actuator，或其後端為 null。
};

const char* to_string(MediaControlStatus s) noexcept;

// 媒體控制 widget 目前顯示的渲染描述 —— 合成 E2-13 中繼資料文字欄位 + E4-02 封面渲染描述 +
// E4-04 四顆控制按鈕的渲染描述。純資料，供後續相位的繪製層消費（本單元不繪製）。
struct MediaControlRenderModel {
    bool has_media = false;  // 是否目前有播放中媒體工作階段（false = 降級 / 無媒體）。
    ds::sysinfo::PlaybackState playback = ds::sysinfo::PlaybackState::Stopped;
    std::string title;
    std::string artist;
    std::string album;
    ds::elements::ImageRenderModel artwork;   // E4-02 封面渲染描述（無封面參照則 has_source=false）。
    MediaButtonRenderModel play_button;       // E4-04（經橋接層中立化；四顆具名控制按鈕的渲染描述）。
    MediaButtonRenderModel pause_button;
    MediaButtonRenderModel prev_button;
    MediaButtonRenderModel next_button;
};

// ---------------------------------------------------------------------------
// MediaControlWidget —— 媒體控制 widget：組裝 C1-01 + E2-13 + E3-03 + E4-02 + E4-04。
//
// 每個實例代表**一個**具名媒體控制 widget（如 "widget.media.home"）。內部持有一個 C1-01
// `SkinProfile` 作為桌面基底（圖層 / 輸入 / 透明外形 / 拖曳全數透傳），並自持四顆
// `MediaControlButtonBridge`（play / pause / prev / next，橋接 E4-04 `ButtonElement`；建構時其
// on_click 已綁定對應的 E3-03 actuator 命令處理器）。行為：`refresh()`（經注入 registry 讀
// E2-13 指標，組裝封面 + 文字 + 按鈕渲染描述）與 `play()` / `pause()` / `next()` / `prev()`
// （模擬對應按鈕一次點擊，觸發其 on_click → 注入的 E3-03 actuator）。
// ---------------------------------------------------------------------------
class MediaControlWidget {
public:
    // 封面的名義固有尺寸（相位 1 不真的解碼影像；來源尺寸為描述性資料，供 E4-02 渲染描述使用）。
    static constexpr int kArtworkSize = 128;

    // 建構一個具名媒體控制 widget。
    //   id        widget 的具名識別碼；同時作為內部 C1-01 基底的 SurfaceId，並衍生封面 /
    //             四顆按鈕的具名目標 / SurfaceId（"<id>.artwork" / ".play" / ".pause" /
    //             ".prev" / ".next"）。
    //   backend/layers  透傳給內部 C1-01 SkinProfile（見其建構子約定），不取得所有權。
    //   registry  注入的 E2-13 指標來源（相位 1 由呼叫端以 MediaMetadataProvider 掛好指標）；
    //             可為 nullptr（`refresh()` 保守降級為「無媒體」）。
    //   actuator  注入的 E3-03 媒體控制致動器（相位 1 為預設綁 NullMediaControlBackend 者）；
    //             可為 nullptr（控制方法能力閘控回 Unavailable）。
    MediaControlWidget(std::string id, ds::kernel::KernelBackend& backend,
                        ds::kernel::LayerStack& layers,
                        std::shared_ptr<ds::metrics::MetricRegistry> registry,
                        std::shared_ptr<ds::actuators::MediaControlActuator> actuator);

    // --- C1-01 基底存取（圖層 / 輸入 / 透明外形 / 拖曳皆透傳，本單元不重造）---
    ds::profiles::SkinProfile& base() noexcept { return base_; }
    const ds::profiles::SkinProfile& base() const noexcept { return base_; }

    const std::string& id() const noexcept { return id_; }

    // --- 刷新（經注入的 E2-13 指標）---
    // 依 id `MediaMetadataProvider::kMetricId`（"media.nowplaying"）於注入的 registry 尋指標：
    //   - registry 為 nullptr，或找不到該指標 → 降級：`has_media()==false`，文字欄位清空、
    //     封面 `has_source=false`、播放狀態回 Stopped，回 `Ok`（無媒體非錯誤）。
    //   - 找到指標但缺少任一必要欄位實例（state/title/artist/album/artwork）→ `Invalid`（結構性
    //     錯誤，不套用，既有 `render_model()` 不變）。
    //   - 欄位齊全但 "media.title" 欄位 `valid==false`（沿用 E2-13 "無播放" 語意）→ 同上降級
    //     路徑，回 `Ok`。
    //   - 有效播放中媒體：讀 state 實例的數值維度換算 `PlaybackState`；讀
    //     title/artist/album/artwork 的文字維度（`valid==false` 視為空字串，不崩）；以 E4-02
    //     `ImageElement` 把非空的封面參照組裝為渲染描述（空參照則 `has_source=false`）；同步
    //     四顆按鈕目前的渲染描述。寫入 `render_model()`。回 `Ok`。
    MediaControlStatus refresh();

    // --- 控制（E4-04 按鈕觸發 E3-03 actuator；經注入後端）---
    // 皆先做能力閘控（NFR-03）：`has_control()==false` → `Unavailable`，不觸發任何按鈕 / 後端、
    // 不改變任何狀態。可用時：對對應具名按鈕模擬一次 press+release（觸發其 `on_click`，該回呼
    // 直接呼叫注入 actuator 的 `handle_play` / `handle_pause` / `handle_next` / `handle_prev`），
    // 並同步 `render_model()` 中四顆按鈕的渲染描述。回 `Ok`。
    MediaControlStatus play();
    MediaControlStatus pause();
    MediaControlStatus next();
    MediaControlStatus prev();

    // 能力閘控查詢（NFR-03）：控制功能於目前注入是否可用（actuator 非 null 且其後端非 null）。
    bool has_control() const noexcept;

    const MediaControlRenderModel& render_model() const noexcept { return model_; }

    // 是否目前有播放中媒體工作階段（反映最後一次 `refresh()`）。
    bool has_media() const noexcept { return model_.has_media; }

    // 診斷：注入的致動器目前完整狀態（播放狀態 + 曲目索引 + 音量）；無 actuator 回預設值。
    ds::actuators::MediaControlState actuator_state() const;

private:
    // 把四顆按鈕目前的渲染描述寫入 model_。
    void sync_buttons();
    // 降級路徑：把 model_ 的媒體相關欄位（除按鈕外）重設為「無媒體」預設，並同步按鈕。
    void apply_no_media();
    // 對指定按鈕模擬一次點擊（觸發其 on_click），先做能力閘控。
    MediaControlStatus trigger(MediaControlButtonBridge& button);

    std::string id_;
    ds::profiles::SkinProfile base_;  // C1-01：桌面基底（圖層 / 輸入 / 透明外形 / 拖曳）。

    std::shared_ptr<ds::metrics::MetricRegistry> registry_;             // E2-13 指標來源（注入式）。
    std::shared_ptr<ds::actuators::MediaControlActuator> actuator_;     // E3-03 控制致動器（注入式）。

    // E4-04（經橋接層）：四顆具名控制按鈕，建構時 on_click 已綁定對應的 E3-03 actuator 命令處理器。
    MediaControlButtonBridge play_button_;
    MediaControlButtonBridge pause_button_;
    MediaControlButtonBridge prev_button_;
    MediaControlButtonBridge next_button_;

    MediaControlRenderModel model_;
};

}  // namespace ds::widgets

#endif  // DS_CONTENT_WIDGETS_C2_07_MEDIA_CONTROL_WIDGET_HPP
