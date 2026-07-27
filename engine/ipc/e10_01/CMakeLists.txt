# engine/ipc/e10_01/CMakeLists.txt — E10-01 本機 IPC 訊息投遞
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e10_01 STATIC message_channel.cpp)
target_include_directories(e10_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e10_01 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：訊息酬載重用其穩定值型別、
# 並可攜帶具名命令。PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者。
target_link_libraries(e10_01 PUBLIC e6_01)

# 測試檔在 tests/e10/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e10_01 ${CMAKE_SOURCE_DIR}/tests/e10/test_e10_01.cpp)
target_link_libraries(test_e10_01 PRIVATE e10_01 GTest::gtest_main)
add_test(NAME e10_01 COMMAND test_e10_01)
