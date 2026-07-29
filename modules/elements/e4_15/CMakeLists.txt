# modules/elements/e4_15/CMakeLists.txt — E4-15 就地輸入框
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_15 STATIC text_input_element.cpp)
target_include_directories(e4_15 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_15 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01：以 TextLayout 排版顯示文字（render_model() 消費 LayoutResult）。
#   - e5_13：以 KeyboardInputSource 接收注入式輸入事件（attach() 訂閱按鍵 / 文字提交頻道）。
# PUBLIC：本單元標頭 include 兩者標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_15 PUBLIC e4_01 e5_13)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_15 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_15.cpp)
target_link_libraries(test_e4_15 PRIVATE e4_15 GTest::gtest_main)
add_test(NAME e4_15 COMMAND test_e4_15)
