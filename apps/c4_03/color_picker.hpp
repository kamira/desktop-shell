// apps/c4_03/color_picker.hpp — C4-03 取色器（artifact 層 / apps，相位 1）
//
// 「取色器」（color picker / eyedropper）：讓使用者從螢幕任一取樣點取色。本單元不是新引擎
// 邏輯，而是把兩個已合併的擴充點/元件**組裝**成單一應用：
//
//   - E2-27（`ds::sysinfo::PixelSampleSource` / `PixelColor`）：於某具名 `ScreenAnchor`
//     讀該點目前的像素顏色（無讀值回 nullopt，本單元據此回報「無效點」）。
//   - E4-30（`ds::elements::DimOverlayElement`）：全螢幕調光覆蓋，本單元借它組出**放大鏡 /
//     取色游標**——顯示覆蓋層並在目前取樣點挖一個具名「不調光」區域，讓使用者看清楚正在
//     取色的那一點（其餘畫面調暗、當前點保持清晰），相位 1 仍是宣告式渲染描述，非真實繪製。
//
// 行為組裝：`pick_at(anchor)` → 顏色（RGB / HEX）；`magnify()` / `dismiss_magnifier()` →
// 放大鏡覆蓋顯隱；`copy()` → 複製目前取到色的 HEX 值到內部剪貼簿（相位 1：不接系統剪貼簿，
// 以 `clipboard()` 供驗證 / 未來相位串接真實剪貼簿）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實螢幕擷取、無真實系統剪貼簿、無平台
// 分支（無 `#ifdef` / win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02，取樣點沿用 E2-27
// 的具名 `ScreenAnchor`）。任何無效操作（無取樣來源、取樣點目前無讀值、尚未取色即放大 /
// 複製）一律明確回傳具名結果，不靜默。
#ifndef DS_APPS_C4_03_COLOR_PICKER_HPP
#define DS_APPS_C4_03_COLOR_PICKER_HPP

#include <memory>
#include <string>

#include "dim_overlay.hpp"   // E4-30（上游，可讀不可改）：DimOverlayElement / DimStatus
#include "screen_pixel.hpp"  // E2-27（上游，可讀不可改）：PixelSampleSource / PixelColor / ScreenAnchor

namespace ds::apps {

// pick_at() 的具名結果。
enum class PickStatus {
    Ok,         // 成功取色。
    NoSource,   // 未設定像素取樣來源（source 為 null）。
    NoReading,  // 該取樣點目前無讀值（未注入假像素 / 取樣失敗）——「無效點」。
};

const char* to_string(PickStatus s) noexcept;

// 放大鏡覆蓋所在具名挖洞區域的字首（NFR-02：具名，非絕對座標）。
// 完整區域名 = kMagnifierRegionPrefix + 該取樣點的 anchor 穩定字串（如 "region.magnifier.center"）。
inline constexpr const char* kMagnifierRegionPrefix = "region.magnifier.";

// 一次成功取色的快照：取樣點 + 顏色 + 兩種顯示格式（RGB / HEX）。
struct ColorPick {
    ds::sysinfo::ScreenAnchor anchor = ds::sysinfo::ScreenAnchor::Center;
    ds::sysinfo::PixelColor color{};
    std::string hex;       // "#RRGGBB"（沿用 E2-27 PixelColor::hex()）。
    std::string rgb_text;  // "rgb(R, G, B)"，供顯示用的另一種色值格式。
};

// ---------------------------------------------------------------------------
// ColorPickerApp —— 取色器應用：組裝 E2-27（像素取樣）+ E4-30（放大鏡覆蓋）。
//
// 以參考持有上游 E4-30 `DimOverlayElement`（不取得所有權，須比本物件活得久）；以
// `shared_ptr` 持有 E2-27 `PixelSampleSource`（沿用 `ScreenPixelProvider` 的相依風格 ——
// null 亦保守不崩，只是每次 `pick_at` 回 `NoSource`）。
// ---------------------------------------------------------------------------
class ColorPickerApp {
public:
    explicit ColorPickerApp(std::shared_ptr<ds::sysinfo::PixelSampleSource> source,
                            ds::elements::DimOverlayElement& magnifier);

    // --- 行為：取色（E2-27）---

    // 於某具名取樣點取色：
    //   - source 為 null → NoSource，**不動**目前已取到的顏色（若曾成功取過）。
    //   - 該點目前無讀值（PixelSampleSource::sample 回 nullopt）→ NoReading，同樣不動狀態。
    //   - 成功 → Ok，`current()` 更新為本次取到的顏色（RGB / HEX 皆備）。
    PickStatus pick_at(ds::sysinfo::ScreenAnchor anchor);

    bool has_source() const noexcept { return static_cast<bool>(source_); }
    bool has_pick() const noexcept { return has_pick_; }

    // 目前（最近一次成功）取到的顏色快照。前提 `has_pick()`；從未成功取色過則回預設值
    // （anchor = Center、顏色為透明黑、hex/rgb_text 為空字串）。
    const ColorPick& current() const noexcept { return current_; }

    // --- 行為：放大鏡 / 取色游標（E4-30 覆蓋層組裝）---

    // 顯示放大鏡：秀出調光覆蓋層，並在目前取樣點挖一個具名「不調光」區域（取色游標）。
    //   - 尚未成功取過色（`has_pick()` 為 false）→ `DimStatus::Invalid`（無點可放大鏡）。
    //   - 取樣點與上次放大鏡所在點不同 → 先移除舊挖洞，避免殘留區域累積。
    //   - per-pixel alpha 能力不可用（NFR-03，委派 E4-30）→ `DimStatus::Unsupported`。
    ds::elements::DimStatus magnify();

    // 收起放大鏡：移除目前挖洞（若有）並隱藏覆蓋層。恆安全，不需能力閘控（委派 E4-30::hide）。
    ds::elements::DimStatus dismiss_magnifier();

    bool magnifier_visible() const;
    // 目前作用中的放大鏡挖洞區域名；未顯示放大鏡時為空字串。
    const std::string& active_magnifier_region() const noexcept { return active_cutout_; }

    // --- 行為：複製 ---

    // 複製目前取到色的 HEX 值到內部剪貼簿（相位 1：不接系統剪貼簿）。
    //   - 尚未成功取色過 → false（no-op，不靜默覆寫剪貼簿為空）。
    //   - 成功 → true，`clipboard()` 更新。
    bool copy();
    const std::string& clipboard() const noexcept { return clipboard_; }
    bool has_copied() const noexcept { return !clipboard_.empty(); }

private:
    std::shared_ptr<ds::sysinfo::PixelSampleSource> source_;
    ds::elements::DimOverlayElement& magnifier_;

    bool has_pick_ = false;
    ColorPick current_{};

    std::string active_cutout_;  // 目前作用中的放大鏡挖洞名；"" = 未顯示。
    std::string clipboard_;
};

}  // namespace ds::apps

#endif  // DS_APPS_C4_03_COLOR_PICKER_HPP
