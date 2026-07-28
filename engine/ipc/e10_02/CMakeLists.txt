# engine/ipc/e10_02/CMakeLists.txt — E10-02 低延遲同機通道
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e10_02 STATIC low_latency_channel.cpp)
target_include_directories(e10_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e10_02 PUBLIC cxx_std_17)

# 相依 E10-01 本機 IPC 訊息通道（已合併，可讀不可改）：Envelope 沿用其 Message，
# 且透過 e10_01 傳遞取得 E6-01 CommandArgs 的 include 目錄。PUBLIC 以傳遞給消費者。
target_link_libraries(e10_02 PUBLIC e10_01)

# 測試檔在 tests/e10/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e10_02 ${CMAKE_SOURCE_DIR}/tests/e10/test_e10_02.cpp)
target_link_libraries(test_e10_02 PRIVATE e10_02 GTest::gtest_main)
add_test(NAME e10_02 COMMAND test_e10_02)
