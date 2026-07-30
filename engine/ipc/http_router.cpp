// E10-03 HTTP 端點 — 契約版本標記。
//
// 路由 / 請求 / 回應邏輯全為 header-only inline（見 http_router.hpp）；本 .cpp 只集中定義
// 契約版本字串，使 STATIC 程式庫具實體符號（與 E6-01 / E10-01 相同作法）。
#include "http_router.hpp"

namespace ds::ipc::http {

const char* contract_version() noexcept { return "e10_03/1.0.0"; }

}  // namespace ds::ipc::http
