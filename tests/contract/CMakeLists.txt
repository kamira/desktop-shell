# tests/contract/CMakeLists.txt — E1-25 kernel 後端契約測試組（所有後端共用）
#
# 根 CMakeLists 以 GLOB 收入 tests/contract/CMakeLists.txt 與 tests/contract/*/CMakeLists.txt
# （見 CHG-20260723-22）—— 本檔放在 tests/contract/ 即會被建置收入，無需改根 CMakeLists。
#
# 平台中立：契約標頭 kernel_backend_contract.hpp 只依賴 E1-24 宣告的 KernelBackend 介面，
# 不含任何平台分支（backend_guard G1b 會掃本目錄的前處理器指令行）。
#
# CHG-20260803-10（修 K-003）：本目標**現在真的 link 一個真實後端**（E1-24 null）。
# 修復前刻意不 link 任何後端以「維持 E1-25 與 E1-24 的單元獨立」，
# 代價是契約從未驗證過任何真實後端——獨立性是手段，不是目的。
# win32 後端的註冊在 src/kernel/backend/win32/ 的 if(WIN32) 目標下（同一份契約標頭）。

add_executable(test_contract_backend test_contract_backend.cpp)

# backend_null 提供 KernelBackend 介面宣告與 null 實作；e1_21 為其上游能力矩陣。
target_link_libraries(test_contract_backend PRIVATE backend_null e1_21 GTest::gtest_main)

target_include_directories(test_contract_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(test_contract_backend PRIVATE cxx_std_17)

add_test(NAME contract_backend COMMAND test_contract_backend)
