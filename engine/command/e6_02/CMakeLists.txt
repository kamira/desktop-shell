# engine/command/e6_02/CMakeLists.txt — E6-02 動作註冊表與前綴/模糊分派（擴充點 3 查詢層）
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e6_02 STATIC action_registry.cpp)
target_include_directories(e6_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e6_02 PUBLIC cxx_std_17)

# 相依上游 E6-01 命令匯流排（已合併，可讀不可改）。PUBLIC 使消費者亦見其 include。
target_link_libraries(e6_02 PUBLIC e6_01)

# 測試檔在 tests/e6/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e6_02 ${CMAKE_SOURCE_DIR}/tests/e6/test_e6_02.cpp)
target_link_libraries(test_e6_02 PRIVATE e6_02 GTest::gtest_main)
add_test(NAME e6_02 COMMAND test_e6_02)
