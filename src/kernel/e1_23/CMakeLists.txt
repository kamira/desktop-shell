# src/kernel/e1_23/CMakeLists.txt — E1-23 分層失敗的手動降級路徑
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_23 STATIC layer_fallback.cpp)
target_include_directories(e1_23 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_23 PUBLIC cxx_std_17)

# 上游相依（可讀不可改）：
#   - e1_01：LayerStack / LayerAssign / layer_capability()（本單元建於其上的手動降級路徑）。
#   - e1_21：CapabilityMatrix / has() 能力閘控（NFR-03；降級策略預設經此判斷可用性）。
# PUBLIC：本單元標頭 include 兩者標頭，故傳遞其 include 目錄。
target_link_libraries(e1_23 PUBLIC e1_01 e1_21)

# 契約 / 單元測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_23 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_23.cpp)
target_link_libraries(test_e1_23 PRIVATE e1_23 GTest::gtest_main)
add_test(NAME e1_23 COMMAND test_e1_23)
