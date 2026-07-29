# content/profiles/c1_05/CMakeLists.txt — C1-05 召喚面板 profile
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_05 STATIC summon_panel_profile.cpp)
target_include_directories(c1_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_05 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝四個擴充點——
#   - e1_02（輸入策略四態）+ e1_14（短暫 profile 生命週期，PUBLIC 相依 e1_02）
#   - e5_05（全域熱鍵註冊）
#   - e7_13（階層式項目結構，PUBLIC 相依 e7_01）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c1_05 PUBLIC e1_02 e1_14 e5_05 e7_13)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_05 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_05.cpp)
target_link_libraries(test_c1_05 PRIVATE c1_05 GTest::gtest_main)
add_test(NAME c1_05 COMMAND test_c1_05)
