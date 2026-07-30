# content/c3_01/CMakeLists.txt — C3-01 啟動器選單樹
# 根 CMakeLists 以 content/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c3_01 STATIC launcher_menu_tree.cpp)
target_include_directories(c3_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c3_01 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：
#   - c1_05（C1-05 召喚面板基底 profile）：本單元掛載其上，重用其選項森林儲存
#     （set_items / items()，本身即透傳 E7-13 build_forest / Item）。
#   - e7_13（階層式項目結構）：經 c1_05 透傳，這裡直接列出以求清楚（Item / Value 直接用於本單元標頭）。
#   - e3_02（啟動程式 / 開檔 / 網頁搜尋致動器）：葉節點選取時透過其具名命令觸發（經其上游
#     e6_01 命令匯流排注入式分派）。
target_link_libraries(c3_01 PUBLIC c1_05 e7_13 e3_02)

# 單元測試在 tests/c3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c3_01 ${CMAKE_SOURCE_DIR}/tests/c3/test_c3_01.cpp)
target_link_libraries(test_c3_01 PRIVATE c3_01 GTest::gtest_main)
add_test(NAME c3_01 COMMAND test_c3_01)
