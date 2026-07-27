// E6-02 動作註冊表與前綴/模糊分派 — 實作
//
// 註冊表、比對與排序邏輯皆為 header-only inline（純資料操作、無平台分支）。
// 本 TU 提供查詢層契約版本標記一個實體符號，使 STATIC 程式庫非空並集中版本字串。
#include "action_registry.hpp"

namespace ds::command {

const char* action_registry_contract_version() noexcept {
    // 擴充點 3「動作」查詢層契約版本。API 面若不相容變更，遞增主版本。
    return "e6_02/1.0.0";
}

}  // namespace ds::command
