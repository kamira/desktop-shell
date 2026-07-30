// E6-01 命令匯流排與分派 — 實作
//
// 匯流排、值型別與結果的邏輯皆為 header-only inline（純資料操作、無平台分支）。
// 本 TU 提供契約版本標記一個實體符號，使 STATIC 程式庫非空並集中版本字串。
#include "command_bus.hpp"

namespace ds::command {

const char* contract_version() noexcept {
    // 擴充點 3「動作」契約版本。API 面若不相容變更，遞增主版本。
    return "e6_01/1.0.0";
}

}  // namespace ds::command
