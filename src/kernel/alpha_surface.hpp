// E1-03 逐像素 alpha surface — 介面擴充 + null 行為（platform 相位 1 = Mac / null 期）
//
// 語意：提供「逐像素 alpha（per-pixel alpha）透明 surface」的能力——surface 可帶
// 每像素透明度（用於不規則形狀 / 半透明桌面元件，如非矩形的桌寵、羽化邊緣的浮層）。
//
// 本單元**不**新增真實繪圖 API，也**不**觸碰任何 OS：per-pixel alpha 是一項**可選能力**
// （NFR-03），使用前一律經上游 E1-24 `KernelBackend::has()`（來源為 E1-21 能力矩陣）閘控；
// 能力不可用時提供結構化「不支援」回報與降級路徑，呼叫端因此永遠安全。
//
// 建於上游 E1-24 `ds::kernel::KernelBackend` surface 模型之上：
//   - surface 一律以**具名 SurfaceId** 指涉（NFR-02：不用數字 handle / index）。
//   - 實體 surface 由 `KernelBackend` 建立 / 銷毀；本層額外在**記憶體**維護每個 surface 的
//     alpha 狀態（`KernelBackend::SurfaceProfile` 為上游固定型別、不含 alpha 欄位，故 alpha
//     通道狀態由本服務層模擬——即相位 1 的「null 後端記憶體模擬」）。
//   - 不含絕對座標：逐像素 alpha 不以 (x,y) 設值，而以**具名模式** + **正規化整體不透明度**
//     [0,1] 表達（不透明度是比例，非座標 / 非數字 z-order，故不違反 NFR-02）。
//
// 相位 1 硬約束：無 `#ifdef` / `win32` / `cocoa` / 真實繪圖 API；能力查詢一律經 has()。
#ifndef DS_KERNEL_E1_03_ALPHA_SURFACE_HPP
#define DS_KERNEL_E1_03_ALPHA_SURFACE_HPP

#include <cstddef>

#include "null_backend.hpp"  // E1-24（上游，可讀不可改）：KernelBackend / SurfaceId / SurfaceProfile

namespace ds::kernel {

// per-pixel alpha 的能力識別碼（NFR-03 能力閘控的鍵）。
//
// 屬**可選能力**：某些桌面環境 / 合成器不支援逐像素透明視窗，故呼叫端必須先 has() 閘控並
// 備妥降級路徑。上游 E1-21 `CapabilityMatrix::defaults()` **未**宣告此鍵——因此在預設（保守）
// 矩陣下 `has()` 回 false（能力不可用）。要在 null 後端模擬「具備此能力」的平台，注入
// `alpha_capable_matrix()`（見下）建構的能力矩陣即可。
inline constexpr const char* kPerPixelAlphaCapability = "surface.per_pixel_alpha";

// surface 的 alpha 合成模式（具名，非數字）。
enum class AlphaMode {
    Opaque,    // 不透明：忽略 alpha 通道，surface 視為實心矩形
    PerPixel,  // 逐像素 alpha：以 surface 自身的 per-pixel alpha 通道合成（不規則 / 羽化）
};

// 一個 alpha surface 的 alpha 狀態 —— 純資料、具名取值、無絕對座標。
//
// `opacity` 為套用在整個 surface 上的**正規化**不透明度乘數 [0.0, 1.0]（1 = 完全不透明，
// 0 = 完全透明）；與逐像素 alpha 通道相乘。它是比例值，非像素座標 / 非數字 z-order。
struct AlphaProfile {
    AlphaMode mode = AlphaMode::PerPixel;
    float opacity = 1.0f;
};

// 操作結果碼 —— 與 E1-25 契約 `Status` 同語意（平台中立、跨後端一致）。
enum class AlphaStatus {
    Ok,           // 操作成功
    Unsupported,  // per-pixel alpha 能力於此後端不可用（has() 閘控回報；呼叫端走降級路徑）
    Invalid,      // 前置條件不滿足（空 / 未知 / 重複 SurfaceId、opacity 非有限值等）
};

// ---------------------------------------------------------------------------
// AlphaSurfaceService —— 逐像素 alpha surface 的能力閘控服務層。
//
// 以參考持有任一 `KernelBackend`（相位 1 為 `NullKernelBackend`；真實後端上線後同介面即可），
// 對外提供「建立支援 per-pixel alpha 的 surface、查詢 / 設定 alpha、生命週期」等操作，全部先經
// `backend.has(kPerPixelAlphaCapability)` 閘控。實體 surface 委由後端 K1 原語建立 / 銷毀；
// 本服務僅**額外**在記憶體維護每個 surface 的 `AlphaProfile`（上游 SurfaceProfile 不含 alpha
// 欄位），即相位 1 的 null 記憶體模擬。呼叫端與真實平台完全解耦。
// ---------------------------------------------------------------------------
class AlphaSurfaceService {
public:
    // 綁定一個後端（不取得所有權；後端須存活於本服務之外的生命週期內）。
    explicit AlphaSurfaceService(KernelBackend& backend) : backend_(backend) {}

    // --- 能力閘控（NFR-03）---
    // per-pixel alpha 能力於綁定後端是否可用。等價於 backend.has(kPerPixelAlphaCapability)。
    // 這是所有 alpha 操作的閘門，也是呼叫端在動作前自檢 / 決定降級路徑的入口。
    bool supported() const { return backend_.has(kPerPixelAlphaCapability); }

    // --- 建立 / 銷毀（K1，經能力閘控）---
    // 建立一個支援 per-pixel alpha 的具名 surface。
    //   - 能力不可用（!supported()）→ 回 Unsupported，且**不**建立任何 surface（降級路徑）。
    //   - id 為空、已是 alpha surface、或後端 create_surface 失敗（含 id 已存在）→ Invalid。
    //   - alpha.opacity 非有限值 → Invalid。成功 → Ok，並記錄（clamp 後的）alpha 狀態。
    AlphaStatus create_alpha_surface(const SurfaceId& id,
                                     const SurfaceProfile& profile,
                                     const AlphaProfile& alpha = {});
    // 銷毀先前建立的 alpha surface（同時銷毀後端實體 surface 並移除 alpha 記錄）。
    // 未知 id 回 Invalid（不崩潰）；成功回 Ok。銷毀不需能力閘控（釋放恆為安全）。
    AlphaStatus destroy_alpha_surface(const SurfaceId& id);

    // 該具名 surface 是否為本服務管理的 alpha surface。
    bool has_alpha_surface(const SurfaceId& id) const { return find(id) != nullptr; }
    // 目前管理的 alpha surface 數量。
    std::size_t alpha_surface_count() const noexcept { return records_.size(); }

    // --- alpha 設定 / 查詢（經能力閘控）---
    // 更新整個 alpha profile（模式 + 不透明度）。能力不可用回 Unsupported；未知 id / opacity
    // 非有限值回 Invalid；成功回 Ok（opacity clamp 至 [0,1]）。
    AlphaStatus set_alpha(const SurfaceId& id, const AlphaProfile& alpha);
    // 僅更新合成模式。閘控 / 錯誤語意同上。
    AlphaStatus set_mode(const SurfaceId& id, AlphaMode mode);
    // 僅更新不透明度 [0,1]（自動 clamp）。閘控 / 錯誤語意同上。
    AlphaStatus set_opacity(const SurfaceId& id, float opacity);

    // 查詢某 alpha surface 目前的 alpha 狀態；未知 id 回 nullptr。
    // 指標於該 surface 存活期間有效。查詢為唯讀，不需能力閘控。
    const AlphaProfile* alpha_profile(const SurfaceId& id) const;
    // 便利查詢：該 surface 是否處於逐像素 alpha 模式；未知 id 保守回 false。
    bool is_per_pixel(const SurfaceId& id) const;

private:
    // 以具名鍵配對記錄（順序即建立順序，永不以數字 index 對外暴露）。
    struct Record {
        SurfaceId id;
        AlphaProfile alpha;
    };
    Record* find(const SurfaceId& id);
    const Record* find(const SurfaceId& id) const;

    KernelBackend& backend_;
    std::vector<Record> records_;
};

// --- 能力矩陣輔助（供測試與未來後端覆用；不修改上游 E1-21）---
//
// 以上游 `CapabilityMatrix::defaults()` 為基礎，追加宣告 per-pixel alpha 為**可用**的可選能力，
// 回傳新矩陣（後定義者為準，故追加項生效）。代表「此平台 / 合成器支援逐像素透明視窗」，
// 注入 `NullKernelBackend` 即可在相位 1 模擬「具備能力」的情境。
CapabilityMatrix alpha_capable_matrix();

// 明確回傳「不具備 per-pixel alpha 能力」的矩陣（即上游保守 `defaults()`：未宣告該鍵，
// has() 回 false）。供降級路徑測試表達「能力不可用」情境。
CapabilityMatrix alpha_incapable_matrix();

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_03_ALPHA_SURFACE_HPP
