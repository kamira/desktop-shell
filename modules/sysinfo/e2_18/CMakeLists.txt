# modules/sysinfo/e2_18/CMakeLists.txt — E2-18 效能計數器（任意）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_18 STATIC perf_counter.cpp)
target_include_directories(e2_18 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_18 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_18 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_18 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_18.cpp)
target_link_libraries(test_e2_18 PRIVATE e2_18 GTest::gtest_main)
add_test(NAME e2_18 COMMAND test_e2_18)
