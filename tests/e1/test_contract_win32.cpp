// W1-01 win32 後端 —— **E1-25 契約套件的註冊點**
//
// 這個檔案就是「相位 1 投資回收點」的實體：契約標頭
// `tests/contract/kernel_backend_contract.hpp` **一個字都沒改**，
// 只是換一個 factory，同一批斷言就套到 win32 真實後端上。
//
// 為什麼放在 `tests/e1/` 而不是 `tests/contract/`：
// `tests/contract/` 的目標在 ubuntu CI 也要建得起來，而 win32 後端只能在 Windows 建。
// 把註冊放在該後端自己的 `if(WIN32)` 目標下，契約本身就能維持平台中立
// （`backend_guard` G1b 會掃 `tests/contract/` 的前處理器指令行）。
//
// 本檔會**真的建立 win32 視窗**：契約斷言的每一條都落在真實 HWND 上，
// 而不是記憶體裡的影子狀態。null 與 win32 對同一組斷言的行為必須一致——
// 若不一致，代表某一方的語意在實作時走偏了，那正是這組契約要抓的東西。
#include "kernel_backend_contract.hpp"
#include "win32_backend.hpp"

namespace {

using ds::kernel::contract::BackendFactory;
using ds::kernel::contract::KernelBackendContract;

}  // namespace

INSTANTIATE_TEST_SUITE_P(
    Win32Backend, KernelBackendContract,
    ::testing::Values(BackendFactory([] {
        return std::unique_ptr<ds::kernel::KernelBackend>(
            new ds::kernel::Win32KernelBackend());
    })),
    [](const ::testing::TestParamInfo<BackendFactory>&) { return std::string("win32"); });
