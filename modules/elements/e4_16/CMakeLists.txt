# modules/elements/e4_16/CMakeLists.txt — E4-16 輸入建議與補全呈現
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_16 STATIC suggestion_popup_element.cpp)
target_include_directories(e4_16 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_16 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01：以 TextLayout 排版每一候選項文字（render_model() 消費 LayoutResult）。
#   - e4_15：綁定 TextInputElement（accept() 套用補全的目標）；其 PUBLIC 連結亦轉遞
#     e5_13（KeyboardInputSource / KeyEvent），供本單元 handle_key()/attach() 使用，
#     無需本單元再另外連結。
# PUBLIC：本單元標頭 include 兩者標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_16 PUBLIC e4_01 e4_15)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_16 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_16.cpp)
target_link_libraries(test_e4_16 PRIVATE e4_16 GTest::gtest_main)
add_test(NAME e4_16 COMMAND test_e4_16)
