# modules/actuators/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e3_01 ====
# modules/actuators/e3_01/CMakeLists.txt — E3-01 外部命令執行致動器
# 根 CMakeLists 會自動 glob 收入本檔（modules/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e3_01 STATIC command_actuator.cpp)
target_include_directories(e3_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_01 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：command.run 具名命令掛上匯流排、參數重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_01 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_01 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_01.cpp)
target_link_libraries(test_e3_01 PRIVATE e3_01 GTest::gtest_main)
add_test(NAME e3_01 COMMAND test_e3_01)

# ==== 單元 e3_02 ====
# modules/actuators/e3_02/CMakeLists.txt — E3-02 啟動程式 / 開檔 / 網頁搜尋致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_02 STATIC launch_actuator.cpp)
target_include_directories(e3_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_02 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：三個具名命令掛上匯流排、參數重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_02 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_02 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_02.cpp)
target_link_libraries(test_e3_02 PRIVATE e3_02 GTest::gtest_main)
add_test(NAME e3_02 COMMAND test_e3_02)

# ==== 單元 e3_03 ====
# modules/actuators/e3_03/CMakeLists.txt — E3-03 媒體播放控制致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_03 STATIC media_control_actuator.cpp)
target_include_directories(e3_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_03 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（掛具名命令、重用穩定值型別）與 E2-13 媒體中繼資料
# （重用平台中立播放狀態詞彙 PlaybackState / state_rank / to_string，與讀取端對稱）。
# 皆已合併，可讀不可改。PUBLIC：本單元對外標頭 include 了 command_bus.hpp 與
# media_metadata.hpp，須把兩上游的 include 目錄向下游（測試）傳遞。
target_link_libraries(e3_03 PUBLIC e6_01 e2_13)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_03 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_03.cpp)
target_link_libraries(test_e3_03 PRIVATE e3_03 GTest::gtest_main)
add_test(NAME e3_03 COMMAND test_e3_03)

# ==== 單元 e3_04 ====
# modules/actuators/e3_04/CMakeLists.txt — E3-04 電源動作致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_04 STATIC power_actuator.cpp)
target_include_directories(e3_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_04 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：五個具名電源命令掛上匯流排、結果重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_04 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_04 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_04.cpp)
target_link_libraries(test_e3_04 PRIVATE e3_04 GTest::gtest_main)
add_test(NAME e3_04 COMMAND test_e3_04)

# ==== 單元 e3_05 ====
# modules/actuators/e3_05/CMakeLists.txt — E3-05 音量設定致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_05 STATIC volume_actuator.cpp)
target_include_directories(e3_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_05 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：五個具名命令掛上匯流排、參數重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_05 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_05 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_05.cpp)
target_link_libraries(test_e3_05 PRIVATE e3_05 GTest::gtest_main)
add_test(NAME e3_05 COMMAND test_e3_05)

# ==== 單元 e3_06 ====
# modules/actuators/e3_06/CMakeLists.txt — E3-06 亮度設定致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_06 STATIC brightness_actuator.cpp)
target_include_directories(e3_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_06 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：三個具名命令掛上匯流排、參數重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_06 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_06 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_06.cpp)
target_link_libraries(test_e3_06 PRIVATE e3_06 GTest::gtest_main)
add_test(NAME e3_06 COMMAND test_e3_06)

# ==== 單元 e3_07 ====
# modules/actuators/e3_07/CMakeLists.txt — E3-07 剪貼簿寫入致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_07 STATIC clipboard_write_actuator.cpp)
target_include_directories(e3_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_07 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：三個具名命令掛上匯流排、參數重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_07 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_07 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_07.cpp)
target_link_libraries(test_e3_07 PRIVATE e3_07 GTest::gtest_main)
add_test(NAME e3_07 COMMAND test_e3_07)

# ==== 單元 e3_08 ====
# modules/actuators/e3_08/CMakeLists.txt — E3-08 網路 / 藍牙開關致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_08 STATIC radio_actuator.cpp)
target_include_directories(e3_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_08 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：八個具名命令掛上匯流排、參數重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_08 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_08 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_08.cpp)
target_link_libraries(test_e3_08 PRIVATE e3_08 GTest::gtest_main)
add_test(NAME e3_08 COMMAND test_e3_08)

# ==== 單元 e3_09 ====
# modules/actuators/e3_09/CMakeLists.txt — E3-09 系統旗標切換致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_09 STATIC system_flag_actuator.cpp)
target_include_directories(e3_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_09 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：四動詞具名命令掛上匯流排、參數重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_09 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_09 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_09.cpp)
target_link_libraries(test_e3_09 PRIVATE e3_09 GTest::gtest_main)
add_test(NAME e3_09 COMMAND test_e3_09)

# ==== 單元 e3_10 ====
# modules/actuators/e3_10/CMakeLists.txt — E3-10 桌布設定致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_10 STATIC wallpaper_actuator.cpp)
target_include_directories(e3_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_10 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：兩個具名命令掛上匯流排、參數重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_10 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_10 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_10.cpp)
target_link_libraries(test_e3_10 PRIVATE e3_10 GTest::gtest_main)
add_test(NAME e3_10 COMMAND test_e3_10)

# ==== 單元 e3_11 ====
# modules/actuators/e3_11/CMakeLists.txt — E3-11 螢幕擷取致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_11 STATIC screen_capture_actuator.cpp)
target_include_directories(e3_11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_11 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：三個具名命令掛上匯流排、參數重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_11 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_11 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_11.cpp)
target_link_libraries(test_e3_11 PRIVATE e3_11 GTest::gtest_main)
add_test(NAME e3_11 COMMAND test_e3_11)
