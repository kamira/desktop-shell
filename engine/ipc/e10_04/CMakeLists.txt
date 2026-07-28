# engine/ipc/e10_04/CMakeLists.txt — E10-04 事件廣播型模組協定
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e10_04 STATIC event_broadcast.cpp)
target_include_directories(e10_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e10_04 PUBLIC cxx_std_17)

# 相依（皆已合併，可讀不可改）：
#   - E10-01 本機 IPC 通道：底層依主題路由 / 扇出 / 投遞計數的廣播機制。
#   - E8-04 行程內模組載入：ModuleId 對接其 manifest.name；模組載入 / 卸載驅動訂閱註冊 / 註銷。
# PUBLIC 以傳遞其 include 目錄給本 target 與消費者（本 target header #include message_channel.hpp）。
target_link_libraries(e10_04 PUBLIC e10_01 e8_04)

# 契約測試在 tests/e10/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e10_04 ${CMAKE_SOURCE_DIR}/tests/e10/test_e10_04.cpp)
target_link_libraries(test_e10_04 PRIVATE e10_04 GTest::gtest_main)
add_test(NAME e10_04 COMMAND test_e10_04)
