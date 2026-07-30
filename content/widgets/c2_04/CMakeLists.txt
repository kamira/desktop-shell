# content/widgets/c2_04/CMakeLists.txt — C2-04 天氣 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_04 STATIC weather_widget.cpp)
target_include_directories(c2_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_04 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝五個擴充點——
#   - c1_01（C1-01 桌面角色皮膚基底 profile）：作為本 widget 的桌面基底，圖層 / 輸入策略 /
#     透明外形 / 自由拖曳皆透傳，本單元不重造。
#   - e2_14（HTTP 取得與結構化解析）：注入式 HttpTransport 擷取天氣端點 + 自帶 JSON 解析
#     （JsonValue / parse_json / JsonPath / seek）。
#   - e4_01（文字排版與渲染）：把格式化後的溫度 / 描述文字排版為渲染描述（TextLayout）。
#   - e4_02（圖片元件）：把天氣圖示代碼組裝為圖片渲染描述（ImageElement / MemoryImageSource）。
#   - e7_10（字串處理）：以 format() 樣板組出可讀顯示文字、以 concat() 組請求 URL。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_04 PUBLIC c1_01 e2_14 e4_01 e4_02 e7_10)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_04 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_04.cpp)
target_link_libraries(test_c2_04 PRIVATE c2_04 GTest::gtest_main)
add_test(NAME c2_04 COMMAND test_c2_04)
