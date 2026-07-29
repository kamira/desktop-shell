# engine/ipc/e10_05/CMakeLists.txt — E10-05 獨立行程 widget 宿主
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e10_05 STATIC widget_host.cpp)
target_include_directories(e10_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e10_05 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e10_01（本機 IPC 訊息投遞）：宿主與主程式的通訊通道（MessageChannel）。
#   - e1_03（逐像素 alpha surface）：widget surface 的協定橋接（AlphaSurfaceService）。
# PUBLIC：本單元標頭 include 兩者標頭，需傳遞 include 目錄給消費者。
target_link_libraries(e10_05 PUBLIC e10_01 e1_03)

# 測試檔在 tests/e10/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e10_05 ${CMAKE_SOURCE_DIR}/tests/e10/test_e10_05.cpp)
target_link_libraries(test_e10_05 PRIVATE e10_05 GTest::gtest_main)
add_test(NAME e10_05 COMMAND test_e10_05)
