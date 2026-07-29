# tests/contract/CMakeLists.txt — E1-25 kernel 後端契約測試組（所有後端共用）
#
# 根 CMakeLists 以 GLOB 收入 tests/contract/CMakeLists.txt 與 tests/contract/*/CMakeLists.txt
# （見 CHG-20260723-22）—— 本檔放在 tests/contract/ 即會被建置收入，無需改根 CMakeLists。
#
# 平台中立：契約僅依賴 kernel 後端抽象介面（本目錄自帶 kernel_backend.hpp）與
# E1-21 能力矩陣，不 link 任何後端實作（含 E1-24 null 後端）—— 契約組自帶測試本地 stub。
#
# 路徑注意：本 CMakeLists 與測試 .cpp / .hpp 同在 tests/contract/，故 .cpp 以相對本目錄
# 的檔名指向即可（與「測試在 tests/<m>/、CMakeLists 在單元目錄」的其他單元不同）。

add_executable(test_contract_backend test_contract_backend.cpp)

# 契約引用 E1-21 CapabilityMatrix（能力查詢契約的資料來源）；e1_21 target 以 PUBLIC
# include dir 匯出 capability_matrix.hpp。GTest::gtest_main 提供 main()。
target_link_libraries(test_contract_backend PRIVATE e1_21 GTest::gtest_main)

# kernel_backend.hpp / stub_backend.hpp 與測試 .cpp 同在本目錄。
target_include_directories(test_contract_backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(test_contract_backend PRIVATE cxx_std_17)

add_test(NAME contract_backend COMMAND test_contract_backend)
