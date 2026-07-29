# modules/elements/e4_11/CMakeLists.txt — E4-11 逐字顯示
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_11 STATIC typewriter_element.cpp)
target_include_directories(e4_11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_11 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01：文字排版（TextLayout / LayoutConstraints / LayoutResult）。
#   - e4_09：動畫驅動源（AnimationDriver / AnimationId / Tick）。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_11 PUBLIC e4_01 e4_09)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_11 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_11.cpp)
target_link_libraries(test_e4_11 PRIVATE e4_11 GTest::gtest_main)
add_test(NAME e4_11 COMMAND test_e4_11)
