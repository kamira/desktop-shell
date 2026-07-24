# engine/render/e4_05/CMakeLists.txt — E4-05 過場動畫與緩動
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e4_05 STATIC easing.cpp tween.cpp)
target_include_directories(e4_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_05 PUBLIC cxx_std_17)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_05 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_05.cpp)
target_link_libraries(test_e4_05 PRIVATE e4_05 GTest::gtest_main)
add_test(NAME e4_05 COMMAND test_e4_05)
