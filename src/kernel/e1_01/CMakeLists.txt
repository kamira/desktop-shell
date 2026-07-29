# src/kernel/e1_01/CMakeLists.txt — E1-01 具名圖層列舉與 z-order 維持
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_01 STATIC layer_stack.cpp)
target_include_directories(e1_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_01 PUBLIC cxx_std_17)

# 上游相依（可讀不可改）：
#   - e1_21：CapabilityMatrix / has() 能力閘控（NFR-03）。
#   - backend_null：具名圖層詞彙 SurfaceLayer / SurfaceId（NFR-02，本單元建於其上）。
# PUBLIC：本單元標頭 include 兩者標頭，故傳遞其 include 目錄。
target_link_libraries(e1_01 PUBLIC e1_21 backend_null)

# 契約 / 單元測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_01 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_01.cpp)
target_link_libraries(test_e1_01 PRIVATE e1_01 GTest::gtest_main)
add_test(NAME e1_01 COMMAND test_e1_01)
