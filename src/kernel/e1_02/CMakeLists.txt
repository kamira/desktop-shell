# src/kernel/e1_02/CMakeLists.txt — E1-02 輸入策略四態
# 根 CMakeLists 以明確層級 GLOB（src/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_02 STATIC input_strategy.cpp)
target_include_directories(e1_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_02 PUBLIC cxx_std_17)

# 上游相依（可讀不可改）：
#   - backend_null（E1-24）：本單元標頭 include `null_backend.hpp`（KernelBackend / InputPolicy
#     / InputEvent / SurfaceId），故 PUBLIC 傳遞其 include 目錄。
#   - e1_21（能力矩陣）：CapabilityId / has()（NFR-03 閘控）。backend_null 已 PUBLIC 依賴之，
#     此處依單元規格顯式再列，語意清晰。
target_link_libraries(e1_02 PUBLIC e1_21 backend_null)

# 單元測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_02 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_02.cpp)
target_link_libraries(test_e1_02 PRIVATE e1_02 GTest::gtest_main)
add_test(NAME e1_02 COMMAND test_e1_02)
