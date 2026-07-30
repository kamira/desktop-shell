# content/widgets/c2_09/CMakeLists.txt — C2-09 行事曆 / 待辦 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_09 STATIC calendar_todo_widget.cpp)
target_include_directories(c2_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_09 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝四個擴充點——
#   - c1_01（`SkinProfile`）：桌面殼層基底（圖層 / 定位 / 拖曳），PUBLIC 相依 e1_01 e1_02 e1_03
#     e1_07 e1_08 e7_01。
#   - e4_01（`TextLayout`）：以文字呈現行事曆 / 待辦清單，PUBLIC 相依 e1_03。
#   - e7_12（`serialize`）：save() 序列化存載，PUBLIC 相依 e7_01。
#   - e7_13（`Item` / `build_forest`）：load() 建構日期分組 → 事項的樹狀結構，PUBLIC 相依 e7_01。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_09 PUBLIC c1_01 e4_01 e7_12 e7_13)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_09 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_09.cpp)
target_link_libraries(test_c2_09 PRIVATE c2_09 GTest::gtest_main)
add_test(NAME c2_09 COMMAND test_c2_09)
