// E1-25 契約測試組 —— null 後端的註冊點
//
// 契約斷言本身在 `kernel_backend_contract.hpp`（平台中立、所有後端共用）。
// 本檔只做一件事：把 E1-24 的 `NullKernelBackend` 註冊進契約套件。
//
// win32 後端的註冊在 `tests/e1/test_contract_win32.cpp`（僅 Windows 建）——
// **同一份契約標頭，不同的註冊點**。這正是 PHASE-PLAN 說的
// 「契約測試不改一行拿去跑新後端」的落地方式；CHG-20260803-10 之前它並不成立（見 K-003）。
//
// 平台中立：本檔不得出現平台條件編譯或平台專屬標頭（backend_guard G1b 會擋）。
#include "kernel_backend_contract.hpp"

namespace {

using ds::kernel::contract::BackendFactory;
using ds::kernel::contract::KernelBackendContract;

}  // namespace

// E1-24 null 後端：相位 1 的參考實作，所有平台皆可建可跑。
INSTANTIATE_TEST_SUITE_P(
    NullBackend, KernelBackendContract,
    ::testing::Values(BackendFactory([] {
        return std::unique_ptr<ds::kernel::KernelBackend>(
            new ds::kernel::NullKernelBackend());
    })),
    [](const ::testing::TestParamInfo<BackendFactory>&) { return std::string("null"); });
