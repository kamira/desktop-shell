// E3-01 外部命令執行致動器 — 實作
//
// 致動器、執行器與請求 / 結果型別的邏輯皆為 header-only inline（純資料操作、無平台分支、
// 無真實 exec / spawn / fork / system）。本 TU 僅提供契約版本標記一個實體符號，
// 使 STATIC 程式庫非空並集中版本字串。
#include "command_actuator.hpp"

namespace ds::actuators {

const char* command_actuator_contract_version() noexcept {
    // 外部命令執行致動器契約版本。API 面若不相容變更，遞增主版本。
    return "e3_01/1.0.0";
}

}  // namespace ds::actuators
