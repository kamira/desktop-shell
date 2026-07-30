// content/profiles/c1_07/dim_overlay_profile.hpp — C1-07 全螢幕調光層 profile
// （artifact 層 / 相位 1：純資料 / 邏輯組裝，無真實 GUI）
//
// 「全螢幕調光層」（screen dim overlay）：一個可套用的整合設定，把已合併的 E4-30
// 全螢幕調光覆蓋元件包成單一應用 profile，涵蓋三種常見情境：
//
//   - Focus（專注模式）：工作時壓暗背景、凸顯目前焦點視窗。
//   - NightShift（夜間調光）：長時間使用時降低整體亮度 / 藍光觀感（暖色調）。
//   - PopupBackdrop（彈窗背景變暗）：跳出對話框 / 彈出選單時壓暗其後的畫面。
//
// 本單元不是新引擎邏輯，而是把兩個已合併的擴充點**組裝**成單一應用 profile：
//
//   - E4-30（`ds::elements::DimOverlayElement`）：調光覆蓋本體 —— 強度 / 顏色 /
//     可見性 / 淡入淡出 / 挖洞，及能力閘控降級（NFR-03）。本單元將其作為成員持有，
//     每個具名情境（`DimProfileKind`）附帶一組預設強度 / 顏色（`default_preset`）。
//   - E1-01（`ds::kernel::LayerStack`，經 E4-30 內部整合）：以**具名頂層**
//     （`SurfaceLayer::Topmost`）置放調光層（NFR-02：具名圖層置頂，非數字 z-order）。
//     本單元的 `layer()` 直接透傳 E4-30 的置頂語意，供驗證組裝正確。
//
// 行為介面收斂為三個動詞：
//   - `activate(intensity)` / `activate()`：套用本情境的預設顏色，並淡入至目標強度
//     （或情境預設強度）—— 即「開始調光」。
//   - `deactivate()`：淡出至 0（即隱藏）—— 即「取消調光」。
//   - `fade(target)`：淡變至任意目標強度（不套用預設顏色，供呼叫端自訂漸變終點）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02）。非有限強度 / 目標一律回
// `DimStatus::Invalid`，不靜默、不改狀態（委派 E4-30 既有語意）。
#ifndef DS_CONTENT_PROFILES_C1_07_DIM_OVERLAY_PROFILE_HPP
#define DS_CONTENT_PROFILES_C1_07_DIM_OVERLAY_PROFILE_HPP

#include <cstddef>
#include <string>

#include "alpha_surface.hpp"  // E1-03（上游，可讀不可改，經 E4-30 傳遞相依）：AlphaSurfaceService
#include "dim_overlay.hpp"    // E4-30（上游，可讀不可改）：DimOverlayElement / DimColor / DimFade /
                               //   DimStatus / DimRenderModel
#include "layer_stack.hpp"    // E1-01（上游，可讀不可改，經 E4-30 傳遞相依）：LayerStack / SurfaceLayer
#include "null_backend.hpp"   // E1-24（上游，可讀不可改，經 E4-30 傳遞相依）：SurfaceId

namespace ds::profiles {

// 調光情境（NFR-02：具名，非數字）。
enum class DimProfileKind {
    Focus,          // 專注模式
    NightShift,     // 夜間調光
    PopupBackdrop,  // 彈窗背景變暗
};

const char* to_string(DimProfileKind kind) noexcept;

// profile 的具名啟用狀態 —— 直接反映底層 E4-30 覆蓋層的可見性（intensity > 0 為 Active）。
enum class DimProfileState {
    Inactive,  // 未啟用 / 已淡出至 0。
    Active,    // 啟用中（覆蓋層可見）。
};

const char* to_string(DimProfileState state) noexcept;

// 一組情境預設值：預設調光強度 + 預設調光顏色。
struct DimPreset {
    float intensity = 0.5f;
    ds::elements::DimColor color{};
};

// 各情境的預設 preset（單一資料來源，供建構與 activate() 無參版取用，亦供測試核對）。
//   - Focus：中度調光、純黑 —— 凸顯焦點、不改變色溫。
//   - NightShift：低度調光、暖色調（降低藍光觀感）。
//   - PopupBackdrop：較深調光、純黑 —— 清楚區隔彈窗與背景。
DimPreset default_preset(DimProfileKind kind) noexcept;

// ---------------------------------------------------------------------------
// DimOverlayProfile —— 全螢幕調光層應用 profile：組裝 E4-30（+ 經其整合的 E1-01）。
//
// 每個實例代表**一個**具名調光情境的覆蓋層（如 "surface.dim.focus"）。以參考持有
// 上游 E1-03 `AlphaSurfaceService` 與 E1-01 `LayerStack`（皆不取得所有權，須比本物件
// 活得久）—— 與 E4-30 `DimOverlayElement` 建構慣例一致。
// ---------------------------------------------------------------------------
class DimOverlayProfile {
public:
    DimOverlayProfile(DimProfileKind kind,
                       ds::kernel::AlphaSurfaceService& alpha,
                       ds::kernel::LayerStack& layers,
                       ds::kernel::SurfaceId surface_id = "surface.dim_overlay");

    // --- 行為：activate / deactivate / fade ---

    // 啟用調光：套用本情境的預設顏色，並淡入至 `intensity`（委派 E4-30 `fade_to`）。
    //   - intensity 非有限 → Invalid，狀態不變（委派 E4-30 語意）。
    //   - per-pixel alpha 能力不可用 → Unsupported（降級路徑，狀態不變）。
    //   - intensity == 0 → 等同 deactivate()（淡出至隱藏）。成功 → Ok。
    ds::elements::DimStatus activate(float intensity);
    // 啟用調光：套用本情境的預設強度（`default_preset(kind()).intensity`）。
    ds::elements::DimStatus activate();

    // 停用調光：淡出至 0（即隱藏）。已停用時再呼叫仍走同一路徑（E4-30 fade_to 冪等安全）。
    ds::elements::DimStatus deactivate();

    // 淡變至任意目標強度，不套用情境預設顏色（供呼叫端自訂漸變終點 / 手動微調）。
    //   - target 非有限 → Invalid；能力不可用 → Unsupported。target>0 視為啟用、
    //     target==0 視為停用（委派 E4-30 `fade_to` 語意）。
    ds::elements::DimStatus fade(float target);

    // --- 挖洞（透傳 E4-30；不調光的具名區域）---
    ds::elements::DimStatus add_cutout(const std::string& region);
    bool remove_cutout(const std::string& region);
    bool has_cutout(const std::string& region) const;
    std::size_t cutout_count() const noexcept;

    // --- 查詢 ---
    DimProfileKind kind() const noexcept { return kind_; }
    DimProfileState state() const noexcept;
    bool is_active() const noexcept;
    float intensity() const noexcept;
    const ds::elements::DimColor& color() const noexcept;
    // NFR-02：本 profile 固定所在的具名頂層（透傳 E4-30，最終來自 E1-01 `SurfaceLayer::Topmost`）。
    ds::kernel::SurfaceLayer layer() const noexcept;
    // NFR-03：per-pixel alpha 能力是否可用（透傳 E4-30，動作前自檢 / 降級路徑入口）。
    bool alpha_supported() const;

    // 相位 1 宣告式渲染描述（直接透傳 E4-30 `render_model()`，供驗證組裝正確）。
    ds::elements::DimRenderModel render_model() const;

private:
    DimProfileKind kind_;
    ds::elements::DimOverlayElement overlay_;
};

}  // namespace ds::profiles

#endif  // DS_CONTENT_PROFILES_C1_07_DIM_OVERLAY_PROFILE_HPP
