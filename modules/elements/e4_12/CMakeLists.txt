# modules/elements/e4_12/CMakeLists.txt — E4-12 可點文字區段
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_12 STATIC clickable_text.cpp)
target_include_directories(e4_12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_12 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01：文字排版引擎（TextLayout / LayoutResult / decode_utf8），取字符相對幾何。
#   - e1_04：幾何命中測試（HitTester / LocalPoint / Shape），判點擊落在哪個字符/區段。
# PUBLIC：本單元標頭 include 兩者標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_12 PUBLIC e4_01 e1_04)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_12 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_12.cpp)
target_link_libraries(test_e4_12 PRIVATE e4_12 GTest::gtest_main)
add_test(NAME e4_12 COMMAND test_e4_12)
