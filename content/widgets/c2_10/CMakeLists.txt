# content/widgets/c2_10/CMakeLists.txt — C2-10 網頁 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_10 STATIC web_widget.cpp)
target_include_directories(c2_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_10 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝兩個擴充點——
#   - c1_01（`SkinProfile`）：桌面殼層基底（圖層 / 定位 / 拖曳），PUBLIC 相依 e1_01 e1_02 e1_03
#     e1_07 e1_08 e7_01。
#   - e10_05（`WidgetHost`）：隔離行程 widget 宿主（本單元「網頁內容」的注入式後端 / 能力閘控
#     過的畫面 surface 橋接），PUBLIC 相依 e10_01 e1_03。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_10 PUBLIC c1_01 e10_05)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_10 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_10.cpp)
target_link_libraries(test_c2_10 PRIVATE c2_10 GTest::gtest_main)
add_test(NAME c2_10 COMMAND test_c2_10)
