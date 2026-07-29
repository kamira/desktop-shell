# content/profiles/c1_04/CMakeLists.txt — C1-04 OSD 浮層 profile
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_04 STATIC osd_overlay_profile.cpp)
target_include_directories(c1_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_04 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝三個擴充點——
#   - e1_01（具名圖層列舉與 z-order 維持，PUBLIC 相依 e1_21 + backend_null）
#   - e1_02（輸入策略四態，PUBLIC 相依 e1_21 + backend_null）
#   - e1_14（短暫 profile 生命週期，PUBLIC 相依 e1_02 + e5_10）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c1_04 PUBLIC e1_01 e1_02 e1_14)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_04 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_04.cpp)
target_link_libraries(test_c1_04 PRIVATE c1_04 GTest::gtest_main)
add_test(NAME c1_04 COMMAND test_c1_04)
