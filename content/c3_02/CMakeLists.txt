# content/c3_02/CMakeLists.txt — C3-02 角色對話本
# 根 CMakeLists 以 content/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c3_02 STATIC dialogue_book.cpp)
target_include_directories(c3_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c3_02 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝三個擴充點——
#   - c1_03（依附角色顯示逐句台詞的氣球 profile；PUBLIC 相依 e1_11 / e1_14 / e4_11 / c1_02）
#   - e8_02（對話腳本直譯器：Script / Step / Interpreter，含條件分支 / 跳轉；PUBLIC 相依 e7_05）
#   - e8_03（表現控制指令集：舞台指示 show/hide/switch_surface/transition/wait 經 CommandBus
#     分派；PUBLIC 相依 e8_02 / e4_06 / e6_01）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c3_02 PUBLIC c1_03 e8_02 e8_03)

# 單元測試在 tests/c3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c3_02 ${CMAKE_SOURCE_DIR}/tests/c3/test_c3_02.cpp)
target_link_libraries(test_c3_02 PRIVATE c3_02 GTest::gtest_main)
add_test(NAME c3_02 COMMAND test_c3_02)
