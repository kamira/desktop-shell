# engine/events/e5_03/CMakeLists.txt — E5-03 生命週期事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_03 STATIC lifecycle_source.cpp)
target_include_directories(e5_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_03 PUBLIC cxx_std_17)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_03 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_03.cpp)
target_link_libraries(test_e5_03 PRIVATE e5_03 GTest::gtest_main)
add_test(NAME e5_03 COMMAND test_e5_03)
