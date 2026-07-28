# engine/ipc/e10_03/CMakeLists.txt — E10-03 HTTP 端點
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e10_03 STATIC http_router.cpp)
target_include_directories(e10_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e10_03 PUBLIC cxx_std_17)

# 相依 E10-01 本機 IPC 訊息通道（已合併，可讀不可改）。PUBLIC 以傳遞 e10_01（及其傳遞的
# e6_01）之 include 目錄給本 target 與其消費者——HTTP 端點以此把請求映射為訊息 / 命令。
target_link_libraries(e10_03 PUBLIC e10_01)

# 測試檔在 tests/e10/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e10_03 ${CMAKE_SOURCE_DIR}/tests/e10/test_e10_03.cpp)
target_link_libraries(test_e10_03 PRIVATE e10_03 GTest::gtest_main)
add_test(NAME e10_03 COMMAND test_e10_03)
