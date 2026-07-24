# engine/metrics/e2_01/CMakeLists.txt — E2-01 統一指標介面（擴充點 1 契約）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e2_01 STATIC metric.cpp)
target_include_directories(e2_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_01 PUBLIC cxx_std_17)

# 契約測試在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_01 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_01.cpp)
target_link_libraries(test_e2_01 PRIVATE e2_01 GTest::gtest_main)
add_test(NAME e2_01 COMMAND test_e2_01)
