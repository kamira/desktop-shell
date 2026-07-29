# modules/elements/e4_13/CMakeLists.txt — E4-13 文字流內嵌物件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_13 STATIC inline_flow_element.cpp)
target_include_directories(e4_13 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_13 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01（文字排版）：重用其 FontMetrics / LayoutConstraints / decode_utf8 做換行/度量。
#   - e4_02（圖片元件）：重用其 ImageRenderModel 做內嵌物件的顯示描述。
# PUBLIC：本單元標頭 include 兩者標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_13 PUBLIC e4_01 e4_02)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_13 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_13.cpp)
target_link_libraries(test_e4_13 PRIVATE e4_13 GTest::gtest_main)
add_test(NAME e4_13 COMMAND test_e4_13)
