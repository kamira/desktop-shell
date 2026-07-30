// content/profiles/c1_06/dock_hot_zone_bridge.hpp — C1-06 內部橋接層：E1-16 邊緣熱區
//
// 把 E1-16 `ds::kernel::EdgeHotZoneRegistry` 包在一個**不 include 任何上游標頭**的介面之後
// （pimpl）。原因見 `dock_types.hpp` 頂部說明：`dock_profile.hpp` 須同時使用 E1-02
// （`input_strategy.hpp`，宣告 `enum class ds::kernel::HitResult`）與 E1-16（透過
// `edge_hot_zone.hpp` → `hit_test.hpp`，宣告 `struct ds::kernel::HitResult`）——兩者標頭
// 若同時出現在同一翻譯單元會因該同名不同型別而編譯失敗。本橋接層的 `.cpp`（且僅有它）
// `#include "edge_hot_zone.hpp"`，藉此把兩個上游 include 樹分隔到不同翻譯單元；本標頭
// 只使用 `dock_types.hpp` 的中立值型別，安全地與 `input_strategy.hpp` 共存於同一翻譯單元。
#ifndef DS_CONTENT_PROFILES_C1_06_DOCK_HOT_ZONE_BRIDGE_HPP
#define DS_CONTENT_PROFILES_C1_06_DOCK_HOT_ZONE_BRIDGE_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "dock_types.hpp"

namespace ds::profiles {

// ---------------------------------------------------------------------------
// DockHotZoneBridge —— 單一具名邊緣熱區的登記 / 命中查詢（委派 E1-16）。
//
// 相位 1：純資料註冊 + 幾何命中，無真實 OS。無狀態相依，僅持有已註冊清單（委派給內部
// E1-16 `EdgeHotZoneRegistry`）。非執行緒安全（呼叫端自行序列化寫入），與上游同語意。
// ---------------------------------------------------------------------------
class DockHotZoneBridge {
public:
    DockHotZoneBridge();
    ~DockHotZoneBridge();

    DockHotZoneBridge(DockHotZoneBridge&&) noexcept;
    DockHotZoneBridge& operator=(DockHotZoneBridge&&) noexcept;
    DockHotZoneBridge(const DockHotZoneBridge&) = delete;
    DockHotZoneBridge& operator=(const DockHotZoneBridge&) = delete;

    // 註冊一個具名邊 / 角的熱區（委派 E1-16 `register_zone`）：無效邊 / 無效厚度比例
    // → 對應 `DockHotZoneStatus`（非 Ok），不註冊、不靜默。
    DockHotZoneStatus register_zone(DockEdge edge, float thickness_ratio, std::string action);

    // 判定本地點是否落入任一已註冊熱區；命中回傳觸發之熱區，否則 `nullopt`。
    std::optional<DockTriggeredZone> test(const DockPoint& point,
                                           const DockScreenExtent& screen) const;

    // 目前已註冊的熱區數量（供測試 / 診斷）。
    std::size_t size() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ds::profiles

#endif  // DS_CONTENT_PROFILES_C1_06_DOCK_HOT_ZONE_BRIDGE_HPP
