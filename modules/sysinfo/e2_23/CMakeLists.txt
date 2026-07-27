# modules/sysinfo/e2_23/CMakeLists.txt — E2-23 檔案系統列舉
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_23 STATIC fs_enum.cpp)
target_include_directories(e2_23 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_23 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面契約（上游已合併，可讀不可改）。
# PUBLIC：本單元對外的標頭 include 了 metric.hpp，須把 e2_01 的 include 目錄向下游傳遞。
target_link_libraries(e2_23 PUBLIC e2_01)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_23 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_23.cpp)
target_link_libraries(test_e2_23 PRIVATE e2_23 GTest::gtest_main)
add_test(NAME e2_23 COMMAND test_e2_23)
