# content/profiles/c1_01/CMakeLists.txt — C1-01 Skin profile（桌面寵物 / 角色皮膚基底 profile）
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_01 STATIC skin_profile.cpp)
target_include_directories(c1_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_01 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝六個擴充點——
#   - e1_01（具名圖層列舉與 z-order 維持，PUBLIC 相依 e1_21 + backend_null）
#   - e1_02（輸入策略四態，PUBLIC 相依 e1_21 + backend_null）
#   - e1_03（逐像素 alpha 透明外形，PUBLIC 相依 e1_21 + backend_null）
#   - e1_07（anchor 定位模型，PUBLIC 相依 e1_03）
#   - e1_08（自由拖曳 + 位置記憶，PUBLIC 相依 e1_03 + e1_07 + e7_12）
#   - e7_01（宣告式格式核心：Value 資料模型，供 load_skin 消費）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c1_01 PUBLIC e1_01 e1_02 e1_03 e1_07 e1_08 e7_01)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_01 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_01.cpp)
target_link_libraries(test_c1_01 PRIVATE c1_01 GTest::gtest_main)
add_test(NAME c1_01 COMMAND test_c1_01)
