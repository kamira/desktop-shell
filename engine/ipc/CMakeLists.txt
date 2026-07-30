# engine/ipc/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e10_01 ====
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

# ==== 單元 e10_02 ====
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

# ==== 單元 e10_03 ====
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

# ==== 單元 e10_04 ====
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

# ==== 單元 e10_05 ====
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
