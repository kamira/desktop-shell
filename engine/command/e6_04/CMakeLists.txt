# engine/command/e6_04/CMakeLists.txt — E6-04 延遲與批次執行（擴充 E6-01 命令匯流排）
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e6_04 STATIC deferred_batch.cpp)
target_include_directories(e6_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e6_04 PUBLIC cxx_std_17)

# 相依上游 E6-01（命令匯流排 / 分派）；上游已合併，可讀不可改。
target_link_libraries(e6_04 PUBLIC e6_01)

# 測試檔在 tests/e6/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e6_04 ${CMAKE_SOURCE_DIR}/tests/e6/test_e6_04.cpp)
target_link_libraries(test_e6_04 PRIVATE e6_04 GTest::gtest_main)
add_test(NAME e6_04 COMMAND test_e6_04)
