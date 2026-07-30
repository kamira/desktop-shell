# content/widgets/c2_08/CMakeLists.txt — C2-08 筆記 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_08 STATIC note_widget.cpp)
target_include_directories(c2_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_08 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝四個擴充點——
#   - c1_01（C1-01 skin profile 基底：桌面存在——圖層 / 輸入策略 / 透明外形 / 定位 + 拖曳；
#     PUBLIC 相依 e1_01 e1_02 e1_03 e1_07 e1_08 e7_01）
#   - e4_01（E4-01 文字排版與渲染，經 e4_15 內部持有；PUBLIC 相依 e1_03）
#   - e4_15（E4-15 就地輸入框：筆記內容的編輯狀態機 + render_model；PUBLIC 相依 e4_01 e5_13）
#   - e7_12（E7-12 設定值寫回：筆記內容序列化 / 還原；PUBLIC 相依 e7_01）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_08 PUBLIC c1_01 e4_01 e4_15 e7_12)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_08 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_08.cpp)
target_link_libraries(test_c2_08 PRIVATE c2_08 GTest::gtest_main)
add_test(NAME c2_08 COMMAND test_c2_08)
