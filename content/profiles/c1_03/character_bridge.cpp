// content/profiles/c1_03/character_bridge.cpp — character_bridge 實作
//
// 本檔是本單元**唯一** `#include "portrait_profile.hpp"`（C1-02）之處——理由見標頭註解：藉由
// 把實際的 C1-02 串接隔離到獨立翻譯單元，避免其 transitively 帶入的 E1-04 `hit_test.hpp`
// （`struct ds::kernel::HitResult`）與 E1-14 transitively 帶入的 E1-02 `input_strategy.hpp`
// （`enum class ds::kernel::HitResult`）在同一翻譯單元內因同名不同型別編譯失敗。本檔不
// `#include "balloon_profile.hpp"` 或 `"transient_profile.hpp"`，故此處不會出現該衝突。
#include "character_bridge.hpp"

#include "portrait_profile.hpp"  // C1-02（上游，可讀不可改）：PortraitProfile

namespace ds::profiles {
namespace detail {

bool character_snapshot(const PortraitProfile& character, std::string& out_id) {
    if (!character.is_loaded()) {
        return false;
    }
    out_id = character.id();
    return true;
}

}  // namespace detail
}  // namespace ds::profiles
