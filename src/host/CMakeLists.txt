# src/host/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e11_01 ====
# src/host/e11_01/CMakeLists.txt — E11-01 系統匣圖示與右鍵選單
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e11_01 STATIC tray.cpp)
target_include_directories(e11_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e11_01 PUBLIC cxx_std_17)

# 依賴 E6-01 命令匯流排（上游，已合併，可讀不可改）；PUBLIC 以傳遞其標頭包含路徑。
target_link_libraries(e11_01 PUBLIC e6_01)

# 契約測試在 tests/e11/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e11_01 ${CMAKE_SOURCE_DIR}/tests/e11/test_e11_01.cpp)
target_link_libraries(test_e11_01 PRIVATE e11_01 GTest::gtest_main)
add_test(NAME e11_01 COMMAND test_e11_01)

# ==== 單元 e11_02 ====
# src/host/e11_02/CMakeLists.txt — E11-02 自繪選單呈現
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e11_02 STATIC menu_renderer.cpp)
target_include_directories(e11_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e11_02 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03：具名 SurfaceId（面板呈現目標，NFR-02）。
#   - e4_01：文字排版（FontMetrics / TextLayout / LayoutResult）；本身已 PUBLIC 連 e1_03。
#   - e7_13：階層式項目結構（Item / 森林），選單樹的建構來源。
# PUBLIC：本單元標頭 include 三者標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e11_02 PUBLIC e1_03 e4_01 e7_13)

# 契約測試在 tests/e11/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e11_02 ${CMAKE_SOURCE_DIR}/tests/e11/test_e11_02.cpp)
target_link_libraries(test_e11_02 PRIVATE e11_02 GTest::gtest_main)
add_test(NAME e11_02 COMMAND test_e11_02)

# ==== 單元 e11_03 ====
# src/host/e11_03/CMakeLists.txt — E11-03 開機自動啟動
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e11_03 STATIC autostart.cpp)
target_include_directories(e11_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e11_03 PUBLIC cxx_std_17)

# 契約測試在 tests/e11/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e11_03 ${CMAKE_SOURCE_DIR}/tests/e11/test_e11_03.cpp)
target_link_libraries(test_e11_03 PRIVATE e11_03 GTest::gtest_main)
add_test(NAME e11_03 COMMAND test_e11_03)
