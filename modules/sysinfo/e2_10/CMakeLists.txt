# modules/sysinfo/e2_10/CMakeLists.txt — E2-10 時間與日期
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_10 STATIC time_date.cpp)
target_include_directories(e2_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_10 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面契約（上游已合併，可讀不可改）。
# PUBLIC：本單元對外的標頭 include 了 metric.hpp，須把 e2_01 的 include 目錄向下游傳遞。
target_link_libraries(e2_10 PUBLIC e2_01)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_10 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_10.cpp)
target_link_libraries(test_e2_10 PRIVATE e2_10 GTest::gtest_main)
add_test(NAME e2_10 COMMAND test_e2_10)
