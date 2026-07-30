# apps/c4_04/CMakeLists.txt — C4-04 視窗總覽
# 根 CMakeLists 以 apps/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c4_04 STATIC window_overview.cpp)
target_include_directories(c4_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c4_04 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝兩個擴充點——
#   - c1_05：召喚面板 profile（總覽本身的基底 profile，借用其開啟/收起短暫生命週期）。
#   - e4_02：圖片元件（每個視窗一個縮圖渲染描述，佔位圖像參照）。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c4_04 PUBLIC c1_05 e4_02)

# 單元測試在 tests/c4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c4_04 ${CMAKE_SOURCE_DIR}/tests/c4/test_c4_04.cpp)
target_link_libraries(test_c4_04 PRIVATE c4_04 GTest::gtest_main)
add_test(NAME c4_04 COMMAND test_c4_04)
