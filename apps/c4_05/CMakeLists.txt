# apps/c4_05/CMakeLists.txt — C4-05 AI 對話面板
# 根 CMakeLists 以 apps/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c4_05 STATIC chat_panel.cpp)
target_include_directories(c4_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c4_05 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝四個擴充點——
#   - c1_05：召喚面板 profile（SummonPanelProfile），供本面板的召喚 / 收起生命週期。
#   - e2_14：HTTP 取得與結構化解析（HttpFetchProvider），供上下文指標。
#   - e4_01：文字排版與渲染（TextLayout / FontMetrics），供草稿量測與訊息渲染描述。
#   - e4_11：逐字顯示（TypewriterElement），對話串每則訊息的顯示元素（內部消費 e4_01）。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c4_05 PUBLIC c1_05 e2_14 e4_01 e4_11)

# 單元測試在 tests/c4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c4_05 ${CMAKE_SOURCE_DIR}/tests/c4/test_c4_05.cpp)
target_link_libraries(test_c4_05 PRIVATE c4_05 GTest::gtest_main)
add_test(NAME c4_05 COMMAND test_c4_05)
