# engine/render/e4_09/CMakeLists.txt — E4-09 動畫驅動源
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e4_09 STATIC animation_driver.cpp)
target_include_directories(e4_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_09 PUBLIC cxx_std_17)

# 相依上游 E5-04 心跳事件（已合併，可讀不可改）：PUBLIC 傳遞其標頭與介面。
target_link_libraries(e4_09 PUBLIC e5_04)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_09 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_09.cpp)
target_link_libraries(test_e4_09 PRIVATE e4_09 GTest::gtest_main)
add_test(NAME e4_09 COMMAND test_e4_09)
