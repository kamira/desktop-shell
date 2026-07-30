# apps/c4_01/CMakeLists.txt — C4-01 鍵位速查
# 根 CMakeLists 以 apps/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c4_01 STATIC shortcut_cheat_sheet.cpp)
target_include_directories(c4_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c4_01 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝兩個擴充點——
#   - c1_04：OSD 浮層 profile（OsdOverlayProfile），本單元掛為速查面板的顯隱基底
#     （show/update/dismiss，PUBLIC 相依 e1_01 + e1_02 + e1_14）。
#   - e4_01：文字排版與渲染（TextLayout / FontMetrics / LayoutResult），供把可見鍵位清單
#     排版成渲染描述（PUBLIC 相依 e1_03）。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c4_01 PUBLIC c1_04 e4_01)

# 單元測試在 tests/c4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c4_01 ${CMAKE_SOURCE_DIR}/tests/c4/test_c4_01.cpp)
target_link_libraries(test_c4_01 PRIVATE c4_01 GTest::gtest_main)
add_test(NAME c4_01 COMMAND test_c4_01)
