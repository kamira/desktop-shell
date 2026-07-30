// content/profiles/c1_03/character_bridge.hpp — C1-03 內部橋接層：讀取依附角色（C1-02）狀態
//
// 手法與 content/profiles/c1_06/dock_hot_zone_bridge.hpp 相同，理由亦相同（既有上游命名碰撞，
// 該處已記錄、本機以 g++ 實測重現）：
//   - C1-02 `portrait_profile.hpp` 經 E5-14 → E5-01 傳遞 `#include "hit_test.hpp"`（E1-04，
//     於 `ds::kernel` 宣告 `struct HitResult`）。
//   - 本單元同時需要 E1-14 `transient_profile.hpp`（經其 `#include "input_strategy.hpp"`
//     （E1-02），於**同一** `ds::kernel` 命名空間宣告**另一個不同型別**的 `enum class
//     HitResult`）。
// 兩份標頭若同時 `#include` 進同一翻譯單元，會因 `ds::kernel::HitResult` 同名不同型別
// （struct 對 enum class）編譯失敗——這是上游既有、本單元 write_scope 之外的命名碰撞，不可
// 修改上游（C1-02 / E1-02 / E1-04 皆已合併，可讀不可改）解決。
//
// 因此把「讀取一個 C1-02 `PortraitProfile` 的目前依附狀態」隔離到獨立翻譯單元：
// `character_bridge.cpp`（且僅有它）才 `#include "portrait_profile.hpp"`；本標頭只使用
// 前置宣告與中立值型別（`std::string`），可安全地與 `balloon_profile.hpp`／E1-14 的標頭共存
// 於同一翻譯單元。
#ifndef DS_CONTENT_PROFILES_C1_03_CHARACTER_BRIDGE_HPP
#define DS_CONTENT_PROFILES_C1_03_CHARACTER_BRIDGE_HPP

#include <string>

namespace ds::profiles {

class PortraitProfile;  // 前置宣告；完整定義見 portrait_profile.hpp（僅 character_bridge.cpp 引入）。

namespace detail {

// 讀取一個角色目前的依附狀態快照：已載入 → 回 true 並把其具名 SurfaceId 寫入 out_id；
// 未載入 → 回 false（不寫 out_id）。純讀取，不持有、不修改 character 的生命週期或狀態。
bool character_snapshot(const PortraitProfile& character, std::string& out_id);

}  // namespace detail
}  // namespace ds::profiles

#endif  // DS_CONTENT_PROFILES_C1_03_CHARACTER_BRIDGE_HPP
