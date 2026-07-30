# content/widgets/c2_01/CMakeLists.txt — C2-01 時鐘 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_01 STATIC clock_widget.cpp)
target_include_directories(c2_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_01 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝四個擴充點——
#   - c1_01（掛載基底：桌面角色皮膚 profile，本 widget 綁定其具名 surface id）
#   - e2_10（時間指標：注入式 TimeSource / CivilTime 純算術分解，無真實壁鐘）
#   - e4_01（文字排版：注入式 FontMetrics，排版為相對佈局 LayoutResult）
#   - e7_01（宣告式格式核心：Value 資料模型，供 configure(Value) 消費）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_01 PUBLIC c1_01 e2_10 e4_01 e7_01)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_01 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_01.cpp)
target_link_libraries(test_c2_01 PRIVATE c2_01 GTest::gtest_main)
add_test(NAME c2_01 COMMAND test_c2_01)
