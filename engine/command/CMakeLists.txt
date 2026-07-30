# engine/command/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e6_01 ====
# engine/command/e6_01/CMakeLists.txt — E6-01 命令匯流排與分派（擴充點 3「動作」）
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e6_01 STATIC command_bus.cpp)
target_include_directories(e6_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e6_01 PUBLIC cxx_std_17)

# 測試檔在 tests/e6/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e6_01 ${CMAKE_SOURCE_DIR}/tests/e6/test_e6_01.cpp)
target_link_libraries(test_e6_01 PRIVATE e6_01 GTest::gtest_main)
add_test(NAME e6_01 COMMAND test_e6_01)

# ==== 單元 e6_02 ====
# engine/command/e6_02/CMakeLists.txt — E6-02 動作註冊表與前綴/模糊分派（擴充點 3 查詢層）
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e6_02 STATIC action_registry.cpp)
target_include_directories(e6_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e6_02 PUBLIC cxx_std_17)

# 相依上游 E6-01 命令匯流排（已合併，可讀不可改）。PUBLIC 使消費者亦見其 include。
target_link_libraries(e6_02 PUBLIC e6_01)

# 測試檔在 tests/e6/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e6_02 ${CMAKE_SOURCE_DIR}/tests/e6/test_e6_02.cpp)
target_link_libraries(test_e6_02 PRIVATE e6_02 GTest::gtest_main)
add_test(NAME e6_02 COMMAND test_e6_02)

# ==== 單元 e6_03 ====
# engine/command/e6_03/CMakeLists.txt — E6-03 條件動作與去抖
# 於 E6-01 命令匯流排之上加條件動作與去抖 / 節流；條件以 E7-05 公式求值。
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e6_03 STATIC conditional_action.cpp)
target_include_directories(e6_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e6_03 PUBLIC cxx_std_17)

# 消費 E6-01（命令匯流排 / 分派）與 E7-05（公式求值引擎）；兩者已合併，可讀不可改。
# PUBLIC 讓下游一併取得其標頭（本單元的公開介面型別引用了兩者）。
target_link_libraries(e6_03 PUBLIC e6_01 e7_05)

# 契約測試在 tests/e6/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e6_03 ${CMAKE_SOURCE_DIR}/tests/e6/test_e6_03.cpp)
target_link_libraries(test_e6_03 PRIVATE e6_03 GTest::gtest_main)
add_test(NAME e6_03 COMMAND test_e6_03)

# ==== 單元 e6_04 ====
# engine/command/e6_04/CMakeLists.txt — E6-04 延遲與批次執行（擴充 E6-01 命令匯流排）
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e6_04 STATIC deferred_batch.cpp)
target_include_directories(e6_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e6_04 PUBLIC cxx_std_17)

# 相依上游 E6-01（命令匯流排 / 分派）；上游已合併，可讀不可改。
target_link_libraries(e6_04 PUBLIC e6_01)

# 測試檔在 tests/e6/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e6_04 ${CMAKE_SOURCE_DIR}/tests/e6/test_e6_04.cpp)
target_link_libraries(test_e6_04 PRIVATE e6_04 GTest::gtest_main)
add_test(NAME e6_04 COMMAND test_e6_04)

# ==== 單元 e6_05 ====
# engine/command/e6_05/CMakeLists.txt — E6-05 跨 surface 控制
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e6_05 STATIC cross_surface_controller.cpp)
target_include_directories(e6_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e6_05 PUBLIC cxx_std_17)

# 上游相依（已合併，可讀不可改）：
#   - e6_01（命令匯流排 CommandBus / CommandArgs / CommandResult）：本單元標頭 include
#     `command_bus.hpp`，經其實際分派命令。
#   - e1_13（多 profile 實例並存 ProfileInstanceRegistry）：本單元標頭 include
#     `profile_instance_registry.hpp`，經其列舉 / 驗證具名目標（InstanceId）。
target_link_libraries(e6_05 PUBLIC e6_01 e1_13)

# 單元測試在 tests/e6/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e6_05 ${CMAKE_SOURCE_DIR}/tests/e6/test_e6_05.cpp)
target_link_libraries(test_e6_05 PRIVATE e6_05 GTest::gtest_main)
add_test(NAME e6_05 COMMAND test_e6_05)
