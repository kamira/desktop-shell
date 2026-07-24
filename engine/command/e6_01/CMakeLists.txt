# engine/command/e6_01/CMakeLists.txt — E6-01 命令匯流排與分派（擴充點 3「動作」）
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e6_01 STATIC command_bus.cpp)
target_include_directories(e6_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e6_01 PUBLIC cxx_std_17)

# 測試檔在 tests/e6/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e6_01 ${CMAKE_SOURCE_DIR}/tests/e6/test_e6_01.cpp)
target_link_libraries(test_e6_01 PRIVATE e6_01 GTest::gtest_main)
add_test(NAME e6_01 COMMAND test_e6_01)
