# engine/package/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e9_01 ====
# engine/package/e9_01/CMakeLists.txt — E9-01 統一套件格式定義（複用 E9-02 manifest）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_01 STATIC package.cpp)
target_include_directories(e9_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_01 PUBLIC cxx_std_17)

# 相依上游單元 E9-02（manifest）：PUBLIC 傳遞其 include 路徑，package.hpp 得以 #include "manifest.hpp"。
target_link_libraries(e9_01 PUBLIC e9_02)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_01 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_01.cpp)
target_link_libraries(test_e9_01 PRIVATE e9_01 GTest::gtest_main)
add_test(NAME e9_01 COMMAND test_e9_01)

# ==== 單元 e9_02 ====
# engine/package/e9_02/CMakeLists.txt — E9-02 manifest（requires/permissions）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_02 STATIC manifest.cpp)
target_include_directories(e9_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_02 PUBLIC cxx_std_17)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_02 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_02.cpp)
target_link_libraries(test_e9_02 PRIVATE e9_02 GTest::gtest_main)
add_test(NAME e9_02 COMMAND test_e9_02)

# ==== 單元 e9_03 ====
# engine/package/e9_03/CMakeLists.txt — E9-03 可互換元件組合（建於 E9-01 元件模型之上）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_03 STATIC composition.cpp)
target_include_directories(e9_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_03 PUBLIC cxx_std_17)

# 相依上游單元 E9-01（套件/元件模型）：PUBLIC 傳遞其 include 路徑，
# composition.hpp 得以 #include "package.hpp"（並經 E9-01 再傳遞 E9-02 的 manifest.hpp）。
target_link_libraries(e9_03 PUBLIC e9_01)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_03 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_03.cpp)
target_link_libraries(test_e9_03 PRIVATE e9_03 GTest::gtest_main)
add_test(NAME e9_03 COMMAND test_e9_03)

# ==== 單元 e9_04 ====
# engine/package/e9_04/CMakeLists.txt — E9-04 主題切換
# 視覺主題的註冊 / 執行期切換 / 變更通知（engine 層，平台中立）。
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_04 STATIC theme.cpp)
target_include_directories(e9_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_04 PUBLIC cxx_std_17)

# 消費 E9-03（可互換元件組合，經其再傳遞 E9-01 / E9-02）與 E7-07（熱重載，經其再傳遞
# E7-01 / E7-06）。PUBLIC 讓下游一併取得其標頭。上游已合併，可讀不可改。
target_link_libraries(e9_04 PUBLIC e9_03 e7_07)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_04 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_04.cpp)
target_link_libraries(test_e9_04 PRIVATE e9_04 GTest::gtest_main)
add_test(NAME e9_04 COMMAND test_e9_04)

# ==== 單元 e9_05 ====
# engine/package/e9_05/CMakeLists.txt — E9-05 佈局存檔與還原
# 桌面元件佈局的具名存檔 / 還原（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e9_05 STATIC layout_store.cpp)
target_include_directories(e9_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_05 PUBLIC cxx_std_17)

# 相依上游 E7-12 設定值寫回（serialize）；E7-01（Value / Document / parse）經其 PUBLIC 傳遞。
target_link_libraries(e9_05 PUBLIC e7_12)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_05 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_05.cpp)
target_link_libraries(test_e9_05 PRIVATE e9_05 GTest::gtest_main)
add_test(NAME e9_05 COMMAND test_e9_05)

# ==== 單元 e9_06 ====
# engine/package/e9_06/CMakeLists.txt — E9-06 套件卸載流程
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_06 STATIC uninstall.cpp)
target_include_directories(e9_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_06 PUBLIC cxx_std_17)

# 相依上游：E9-01（套件格式，PUBLIC 傳遞 package.hpp / 並經其傳遞 E9-02 manifest.hpp）。
# 已合併於 main，可讀不可改。
target_link_libraries(e9_06 PUBLIC e9_01)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_06 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_06.cpp)
target_link_libraries(test_e9_06 PRIVATE e9_06 GTest::gtest_main)
add_test(NAME e9_06 COMMAND test_e9_06)

# ==== 單元 e9_07 ====
# engine/package/e9_07/CMakeLists.txt — E9-07 拖放安裝
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_07 STATIC drop_install.cpp)
target_include_directories(e9_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_07 PUBLIC cxx_std_17)

# 相依上游：E9-01（套件格式，PUBLIC 傳遞 package.hpp / 並經其傳遞 E9-02 manifest.hpp）、
# E5-08（系統事件，PUBLIC 傳遞 system_event.hpp）。二者已合併於 main，可讀不可改。
target_link_libraries(e9_07 PUBLIC e9_01 e5_08)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_07 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_07.cpp)
target_link_libraries(test_e9_07 PRIVATE e9_07 GTest::gtest_main)
add_test(NAME e9_07 COMMAND test_e9_07)

# ==== 單元 e9_08 ====
# engine/package/e9_08/CMakeLists.txt — E9-08 圖形安裝器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_08 STATIC install_wizard.cpp)
target_include_directories(e9_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_08 PUBLIC cxx_std_17)

# 相依上游：E9-07（拖放安裝，PUBLIC 傳遞 drop_install.hpp，並經其傳遞 E9-01 / E9-02 / E5-08）。
# 已合併於 main，可讀不可改。
target_link_libraries(e9_08 PUBLIC e9_07)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_08 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_08.cpp)
target_link_libraries(test_e9_08 PRIVATE e9_08 GTest::gtest_main)
add_test(NAME e9_08 COMMAND test_e9_08)

# ==== 單元 e9_09 ====
# engine/package/e9_09/CMakeLists.txt — E9-09 動態配色生成
# 從種子色動態生成一整套協調配色（主/次/強調/背景/前景/狀態色），橋接 E9-04 主題。
# engine 層、平台中立、純邏輯。根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_09 STATIC palette.cpp)
target_include_directories(e9_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_09 PUBLIC cxx_std_17)

# 消費 E9-04（主題切換 / ThemeData；經其再傳遞 E9-03 / E7-07 → E7-01 等）。
# PUBLIC 讓下游一併取得其標頭。上游已合併，可讀不可改。
target_link_libraries(e9_09 PUBLIC e9_04)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_09 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_09.cpp)
target_link_libraries(test_e9_09 PRIVATE e9_09 GTest::gtest_main)
add_test(NAME e9_09 COMMAND test_e9_09)
