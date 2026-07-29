# src/host/e11_02/CMakeLists.txt — E11-02 自繪選單呈現
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e11_02 STATIC menu_renderer.cpp)
target_include_directories(e11_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e11_02 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03：具名 SurfaceId（面板呈現目標，NFR-02）。
#   - e4_01：文字排版（FontMetrics / TextLayout / LayoutResult）；本身已 PUBLIC 連 e1_03。
#   - e7_13：階層式項目結構（Item / 森林），選單樹的建構來源。
# PUBLIC：本單元標頭 include 三者標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e11_02 PUBLIC e1_03 e4_01 e7_13)

# 契約測試在 tests/e11/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e11_02 ${CMAKE_SOURCE_DIR}/tests/e11/test_e11_02.cpp)
target_link_libraries(test_e11_02 PRIVATE e11_02 GTest::gtest_main)
add_test(NAME e11_02 COMMAND test_e11_02)
