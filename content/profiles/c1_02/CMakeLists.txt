# content/profiles/c1_02/CMakeLists.txt — C1-02 立繪 profile
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_02 STATIC portrait_profile.cpp)
target_include_directories(c1_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_02 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝六個擴充點——
#   - e1_01（具名圖層列舉與 z-order 維持）+ e1_03（逐像素 alpha surface，經其傳遞 e1_21 /
#     backend_null）+ e1_08（自由拖曳與位置記憶，PUBLIC 相依 e1_03 / e1_07 / e7_12）
#   - e4_06（Surface 編號定址與切換，PUBLIC 相依 e4_02 圖片元件，經其傳遞 e1_03）
#   - e5_14（碰撞區域名事件參數，PUBLIC 相依 e5_01 / e1_05）
#   - e7_01（宣告式格式核心，亦經 e1_08 -> e7_12 透傳）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c1_02 PUBLIC e1_01 e1_03 e1_08 e4_06 e5_14 e7_01)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_02 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_02.cpp)
target_link_libraries(test_c1_02 PRIVATE c1_02 GTest::gtest_main)
add_test(NAME c1_02 COMMAND test_c1_02)
