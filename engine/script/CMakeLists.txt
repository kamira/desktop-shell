# engine/script/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e8_01 ====
# engine/script/e8_01/CMakeLists.txt — E8-01 腳本引擎（QML/JS）
# 描述子系統的腳本引擎抽象與嵌入介面（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e8_01 STATIC script_engine.cpp)
target_include_directories(e8_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e8_01 PUBLIC cxx_std_17)

# 消費 E6-01 的命令匯流排（ScriptValue 沿用其 CommandValue；命令橋接分派 CommandBus）；
# 已合併，可讀不可改。PUBLIC 讓下游一併取得其標頭。
target_link_libraries(e8_01 PUBLIC e6_01)

# 契約測試在 tests/e8/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e8_01 ${CMAKE_SOURCE_DIR}/tests/e8/test_e8_01.cpp)
target_link_libraries(test_e8_01 PRIVATE e8_01 GTest::gtest_main)
add_test(NAME e8_01 COMMAND test_e8_01)

# ==== 單元 e8_02 ====
# engine/script/e8_02/CMakeLists.txt — E8-02 對話腳本直譯器
# 描述子系統的腳本直譯層（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e8_02 STATIC interpreter.cpp)
target_include_directories(e8_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e8_02 PUBLIC cxx_std_17)

# 消費 E7-05 的公式運算引擎（且傳遞取得 E7-02 的 VariableScope / E7-01 的 Value / resolve）；
# 已合併，可讀不可改。PUBLIC 讓下游一併取得其標頭。
target_link_libraries(e8_02 PUBLIC e7_05)

# 契約測試在 tests/e8/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e8_02 ${CMAKE_SOURCE_DIR}/tests/e8/test_e8_02.cpp)
target_link_libraries(test_e8_02 PRIVATE e8_02 GTest::gtest_main)
add_test(NAME e8_02 COMMAND test_e8_02)

# ==== 單元 e8_03 ====
# engine/script/e8_03/CMakeLists.txt — E8-03 表現控制指令集
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e8_03 STATIC presentation_commands.cpp)
target_include_directories(e8_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e8_03 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e8_02：與 E8-02 對話腳本直譯器整合的介面（OutputSink），本單元的 PresentationSink
#     實作它，讓既有 say 步驟可編排表現指令。
#   - e4_06：switch_surface 指令委派給 SurfaceSwitcher 做實際切換。
#   - e6_01：show/hide/switch_surface/transition/wait 一律經 CommandBus 具名分派。
# PUBLIC：本單元標頭 include 三者的標頭，需傳遞其 include 目錄 / 型別。
target_link_libraries(e8_03 PUBLIC e8_02 e4_06 e6_01)

# 測試檔在 tests/e8/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e8_03 ${CMAKE_SOURCE_DIR}/tests/e8/test_e8_03.cpp)
target_link_libraries(test_e8_03 PRIVATE e8_03 GTest::gtest_main)
add_test(NAME e8_03 COMMAND test_e8_03)

# ==== 單元 e8_04 ====
# engine/script/e8_04/CMakeLists.txt — E8-04 行程內模組載入（擴充點 5 的載入機制）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e8_04 STATIC module_loader.cpp)
target_include_directories(e8_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e8_04 PUBLIC cxx_std_17)

# 相依 E9-02 manifest（已合併，可讀不可改）。PUBLIC：本單元 header 直接 #include
# manifest.hpp 並在對外 API（Module::manifest）中暴露 ds::package 型別。
target_link_libraries(e8_04 PUBLIC e9_02)

# 契約測試在 tests/e8/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e8_04 ${CMAKE_SOURCE_DIR}/tests/e8/test_e8_04.cpp)
target_link_libraries(test_e8_04 PRIVATE e8_04 GTest::gtest_main)
add_test(NAME e8_04 COMMAND test_e8_04)

# ==== 單元 e8_05 ====
# engine/script/e8_05/CMakeLists.txt — E8-05 外部模組開發介面（SDK；擴充點 5 的開發面）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e8_05 STATIC module_sdk.cpp)
target_include_directories(e8_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e8_05 PUBLIC cxx_std_17)

# 相依 E8-04 載入機制（已合併，可讀不可改）。PUBLIC：本單元 header 直接 #include
# module_loader.hpp 並在對外 API（make_module → ds::ext::Module、ModuleContext 橋接
# ds::ext::ModuleContext）中暴露 ds::ext 型別。E9-02 manifest 型別經 e8_04 PUBLIC 傳遞。
target_link_libraries(e8_05 PUBLIC e8_04)

# 契約測試在 tests/e8/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e8_05 ${CMAKE_SOURCE_DIR}/tests/e8/test_e8_05.cpp)
target_link_libraries(test_e8_05 PRIVATE e8_05 GTest::gtest_main)
add_test(NAME e8_05 COMMAND test_e8_05)
