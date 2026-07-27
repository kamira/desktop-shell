# engine/metrics/e2_02/CMakeLists.txt — E2-02 採集頻率分級與除頻
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e2_02 STATIC sampling.cpp)
target_include_directories(e2_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_02 PUBLIC cxx_std_17)

# 相依 E2-01 統一指標介面（沿用 MetricId 作指標身分）。PUBLIC：本單元的公開標頭
# include 了 metric.hpp，故傳遞相依給下游消費者。
target_link_libraries(e2_02 PUBLIC e2_01)

# 契約測試在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_02 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_02.cpp)
target_link_libraries(test_e2_02 PRIVATE e2_02 GTest::gtest_main)
add_test(NAME e2_02 COMMAND test_e2_02)
