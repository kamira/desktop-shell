# modules/sysinfo/e2_17/CMakeLists.txt — E2-17 主機板感測器（溫度 / 風扇 / 電壓）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_17 STATIC board_sensors.cpp)
target_include_directories(e2_17 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_17 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_17 PUBLIC e2_01 e2_02)

# 宣告相依 E10-01（本機 IPC 訊息投遞）：E10-01 為 IPC 通道、**非**感測器 / 裝置列舉
# 基礎設施，本單元感測器列舉自帶可注入抽象、不 include 其標頭；此處以 PRIVATE 連結以
# 忠實反映 units.json 宣告的相依邊，而不對下游傳遞其 include 目錄。
target_link_libraries(e2_17 PRIVATE e10_01)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_17 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_17.cpp)
target_link_libraries(test_e2_17 PRIVATE e2_17 GTest::gtest_main)
add_test(NAME e2_17 COMMAND test_e2_17)
