# src/events/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e5_05 ====
# src/events/e5_05/CMakeLists.txt — E5-05 全域熱鍵註冊
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_05 STATIC global_hotkey.cpp)
target_include_directories(e5_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_05 PUBLIC cxx_std_17)

# 契約測試在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_05 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_05.cpp)
target_link_libraries(test_e5_05 PRIVATE e5_05 GTest::gtest_main)
add_test(NAME e5_05 COMMAND test_e5_05)

# ==== 單元 e5_08 ====
# src/events/e5_08/CMakeLists.txt — E5-08 系統事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_08 STATIC system_event.cpp)
target_include_directories(e5_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_08 PUBLIC cxx_std_17)

# 契約測試在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_08 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_08.cpp)
target_link_libraries(test_e5_08 PRIVATE e5_08 GTest::gtest_main)
add_test(NAME e5_08 COMMAND test_e5_08)

# ==== 單元 e5_12 ====
# src/events/e5_12/CMakeLists.txt — E5-12 全域指標手勢
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_12 STATIC global_gesture.cpp)
target_include_directories(e5_12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_12 PUBLIC cxx_std_17)

# 上游相依：E1-21 能力矩陣（已合併，可讀不可改）——PUBLIC 使其 include 目錄
# 一併傳遞給連結本庫的測試目標。
target_link_libraries(e5_12 PUBLIC e1_21)

# 契約測試在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_12 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_12.cpp)
target_link_libraries(test_e5_12 PRIVATE e5_12 GTest::gtest_main)
add_test(NAME e5_12 COMMAND test_e5_12)
