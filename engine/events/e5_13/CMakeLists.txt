# engine/events/e5_13/CMakeLists.txt — E5-13 鍵盤與輸入提交事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_13 STATIC keyboard_input.cpp)
target_include_directories(e5_13 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_13 PUBLIC cxx_std_17)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_13 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_13.cpp)
target_link_libraries(test_e5_13 PRIVATE e5_13 GTest::gtest_main)
add_test(NAME e5_13 COMMAND test_e5_13)
