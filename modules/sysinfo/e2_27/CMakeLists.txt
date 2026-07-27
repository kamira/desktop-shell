# modules/sysinfo/e2_27/CMakeLists.txt — E2-27 螢幕像素取樣
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_27 STATIC screen_pixel.cpp)
target_include_directories(e2_27 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_27 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集分級（上游已合併，可讀不可改）。
# PUBLIC：本單元標頭 include 了 metric.hpp 與 sampling.hpp，須把上游的 include 目錄
# 向下游傳遞。
target_link_libraries(e2_27 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_27 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_27.cpp)
target_link_libraries(test_e2_27 PRIVATE e2_27 GTest::gtest_main)
add_test(NAME e2_27 COMMAND test_e2_27)
