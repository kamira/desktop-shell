// E3-03 媒體播放控制致動器 — 實作
//
// 致動器、後端與狀態型別的邏輯皆為 header-only inline（純資料操作、無平台分支、
// 無真實媒體控制 API）。本 TU 僅提供契約版本標記一個實體符號，使 STATIC 程式庫非空
// 並集中版本字串。
#include "media_control_actuator.hpp"

namespace ds::actuators {

const char* media_control_contract_version() noexcept {
    // 媒體播放控制致動器契約版本。API 面若不相容變更，遞增主版本。
    return "e3_03/1.0.0";
}

}  // namespace ds::actuators
