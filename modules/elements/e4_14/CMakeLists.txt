# modules/elements/e4_14/CMakeLists.txt — E4-14 長文捲動與分頁
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_14 STATIC scroll_view.cpp)
target_include_directories(e4_14 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_14 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01：文字排版結果 LayoutResult（`set_content` 取用其 `lines.size()` 作為捲動範圍）。
# PUBLIC：本單元標頭 include 上游標頭 text_layout.hpp，需傳遞其 include 目錄給下游。
target_link_libraries(e4_14 PUBLIC e4_01)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_14 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_14.cpp)
target_link_libraries(test_e4_14 PRIVATE e4_14 GTest::gtest_main)
add_test(NAME e4_14 COMMAND test_e4_14)
