// content/profiles/c1_06/dock_types.hpp — C1-06 Dock profile 內部共用值型別
//
// 為何需要一套「自有」邊緣 / 幾何詞彙，而不是直接沿用 E1-16 的 `EdgeHotZone` /
// `ScreenExtent`：本單元須在同一個 `DockProfile`（`dock_profile.hpp`）內同時組裝
// E1-02（`input_strategy.hpp`，於 `ds::kernel` 宣告 `enum class HitResult`）與
// E1-16（`edge_hot_zone.hpp` → 內部 `#include "hit_test.hpp"`，於同一 `ds::kernel`
// 命名空間宣告**另一個不同型別**的 `struct HitResult`）。這兩個上游擴充點各自獨立、
// 各自已合併、彼此互不相依，但**兩者的標頭若在同一翻譯單元內同時 `#include`，會因
// `ds::kernel::HitResult` 同名不同型別（enum class 對 struct）而編譯失敗**（已於本機
// 以 g++ 實測重現：`use of 'HitResult' with tag type that does not match previous
// declaration`）。這是上游既有、本單元 write_scope 之外的命名碰撞，不可修改上游解決。
//
// 因此本單元把「E1-16 熱區」的實際串接**隔離到獨立翻譯單元**（見 `dock_hot_zone_bridge.*`）：
// 該橋接層的標頭只使用本檔宣告的中立值型別（不 include 任何上游標頭），只有其 `.cpp` 才
// `#include "edge_hot_zone.hpp"`（本單元唯一一處）。`dock_profile.hpp` 因此可以安全地同時
// `#include "input_strategy.hpp"`（E1-02）與 `dock_hot_zone_bridge.hpp`（不透明橋接），
// 兩者共存於同一翻譯單元不會觸發上述碰撞。
//
// 相位 1 約束（NFR-02）：邊一律具名，厚度 / 點一律相對 / 比例，非螢幕絕對座標。
#ifndef DS_CONTENT_PROFILES_C1_06_DOCK_TYPES_HPP
#define DS_CONTENT_PROFILES_C1_06_DOCK_TYPES_HPP

#include <string>

namespace ds::profiles {

// 具名邊 / 角——鏡射 E1-16 `ds::kernel::EdgeHotZone` 的具名集合（值一一對應，橋接層負責
// 轉換）。維持獨立型別是隔離上述命名碰撞的必要手段，非重複造輪子。
enum class DockEdge {
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

// 本地點——與 E1-04 `LocalPoint` 同語意（元件本地座標，非螢幕絕對座標，NFR-02）。
struct DockPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// 螢幕尺寸——與 E1-16 `ScreenExtent` 同語意（尺寸而非螢幕於桌面之絕對位置）。
struct DockScreenExtent {
    float width = 0.0f;
    float height = 0.0f;
};

// 鏡射 E1-16 `HotZoneStatus`——`register_zone` 的結構化結果，報錯不靜默。
enum class DockHotZoneStatus {
    Ok,
    InvalidZone,
    InvalidThickness,
};

// 鏡射 E1-16 `TriggeredHotZone`——`test()` 命中時的觸發結果。
struct DockTriggeredZone {
    DockEdge edge = DockEdge::Left;
    std::string action;
};

}  // namespace ds::profiles

#endif  // DS_CONTENT_PROFILES_C1_06_DOCK_TYPES_HPP
