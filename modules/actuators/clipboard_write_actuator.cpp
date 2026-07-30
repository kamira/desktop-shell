// E3-07 剪貼簿寫入致動器 — 實作
//
// 致動器、後端與資料型別的邏輯皆為 header-only inline（純資料操作、無平台分支、
// 無真實剪貼簿 API）。本 TU 僅提供契約版本標記一個實體符號，使 STATIC 程式庫非空
// 並集中版本字串。
#include "clipboard_write_actuator.hpp"

namespace ds::actuators {

const char* clipboard_write_contract_version() noexcept {
    // 剪貼簿寫入致動器契約版本。API 面若不相容變更，遞增主版本。
    return "e3_07/1.0.0";
}

}  // namespace ds::actuators
