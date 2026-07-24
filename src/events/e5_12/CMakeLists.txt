# src/events/e5_12/CMakeLists.txt — E5-12 全域指標手勢
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_12 STATIC global_gesture.cpp)
target_include_directories(e5_12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_12 PUBLIC cxx_std_17)

# 上游相依：E1-21 能力矩陣（已合併，可讀不可改）——PUBLIC 使其 include 目錄
# 一併傳遞給連結本庫的測試目標。
target_link_libraries(e5_12 PUBLIC e1_21)

# 契約測試在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_12 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_12.cpp)
target_link_libraries(test_e5_12 PRIVATE e5_12 GTest::gtest_main)
add_test(NAME e5_12 COMMAND test_e5_12)
