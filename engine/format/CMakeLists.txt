# engine/format/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e7_01 ====
# engine/format/e7_01/CMakeLists.txt — E7-01 宣告式格式核心 + 版本欄位
# 描述子系統的根契約（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_01 STATIC document.cpp)
target_include_directories(e7_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_01 PUBLIC cxx_std_17)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_01 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_01.cpp)
target_link_libraries(test_e7_01 PRIVATE e7_01 GTest::gtest_main)
add_test(NAME e7_01 COMMAND test_e7_01)

# ==== 單元 e7_02 ====
# engine/format/e7_02/CMakeLists.txt — E7-02 變數系統
# 描述子系統的變數層（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_02 STATIC variables.cpp)
target_include_directories(e7_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_02 PUBLIC cxx_std_17)

# 消費 E7-01 的 Value / Document（已合併，可讀不可改）。PUBLIC 讓下游一併取得其標頭。
target_link_libraries(e7_02 PUBLIC e7_01)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_02 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_02.cpp)
target_link_libraries(test_e7_02 PRIVATE e7_02 GTest::gtest_main)
add_test(NAME e7_02 COMMAND test_e7_02)

# ==== 單元 e7_03 ====
# engine/format/e7_03/CMakeLists.txt — E7-03 段落變數
# 描述子系統的「變數段落」層（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_03 STATIC section_vars.cpp)
target_include_directories(e7_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_03 PUBLIC cxx_std_17)

# 消費 E7-02（VariableScope / resolve；其又 PUBLIC 帶入 E7-01 的 Value / Document）。
# 上游已合併，可讀不可改。PUBLIC 讓下游一併取得其標頭。
target_link_libraries(e7_03 PUBLIC e7_02)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_03 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_03.cpp)
target_link_libraries(test_e7_03 PRIVATE e7_03 GTest::gtest_main)
add_test(NAME e7_03 COMMAND test_e7_03)

# ==== 單元 e7_04 ====
# engine/format/e7_04/CMakeLists.txt — E7-04 動態變數與執行期重算
# 描述子系統的「反應式變數」層（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_04 STATIC reactive.cpp)
target_include_directories(e7_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_04 PUBLIC cxx_std_17)

# 消費 E7-02（VariableScope；其又 PUBLIC 帶入 E7-01 的 Value / Document）。
# 上游已合併，可讀不可改。PUBLIC 讓下游一併取得其標頭。
target_link_libraries(e7_04 PUBLIC e7_02)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_04 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_04.cpp)
target_link_libraries(test_e7_04 PRIVATE e7_04 GTest::gtest_main)
add_test(NAME e7_04 COMMAND test_e7_04)

# ==== 單元 e7_05 ====
# engine/format/e7_05/CMakeLists.txt — E7-05 公式運算引擎
# 描述子系統的公式 / 運算式求值層（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_05 STATIC formula.cpp)
target_include_directories(e7_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_05 PUBLIC cxx_std_17)

# 消費 E7-02 的 VariableScope（且傳遞取得 E7-01 的 Value / Document）；已合併，可讀不可改。
# PUBLIC 讓下游一併取得其標頭。
target_link_libraries(e7_05 PUBLIC e7_02)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_05 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_05.cpp)
target_link_libraries(test_e7_05 PRIVATE e7_05 GTest::gtest_main)
add_test(NAME e7_05 COMMAND test_e7_05)

# ==== 單元 e7_06 ====
# engine/format/e7_06/CMakeLists.txt — E7-06 錯誤可見性
# 描述子系統的錯誤呈現層（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_06 STATIC visibility.cpp)
target_include_directories(e7_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_06 PUBLIC cxx_std_17)

# 消費 E7-01 的 ParseError / ParseResult（已合併，可讀不可改）。PUBLIC 讓下游一併取得其標頭。
target_link_libraries(e7_06 PUBLIC e7_01)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_06 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_06.cpp)
target_link_libraries(test_e7_06 PRIVATE e7_06 GTest::gtest_main)
add_test(NAME e7_06 COMMAND test_e7_06)

# ==== 單元 e7_07 ====
# engine/format/e7_07/CMakeLists.txt — E7-07 熱重載
# 描述子系統的熱重載生命週期（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_07 STATIC hot_reload.cpp)
target_include_directories(e7_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_07 PUBLIC cxx_std_17)

# 消費 E7-01（Document/parse/ParseError）與 E7-06（Diagnostic/format_report）。
# PUBLIC 讓下游一併取得其標頭。上游已合併，可讀不可改。
target_link_libraries(e7_07 PUBLIC e7_01 e7_06)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_07 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_07.cpp)
target_link_libraries(test_e7_07 PRIVATE e7_07 GTest::gtest_main)
add_test(NAME e7_07 COMMAND test_e7_07)

# ==== 單元 e7_08 ====
# engine/format/e7_08/CMakeLists.txt — E7-08 設定遷移與版本相容
# 描述子系統的版本升級鏈（engine 層，平台中立純邏輯）。
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e7_08 STATIC migration.cpp)
target_include_directories(e7_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_08 PUBLIC cxx_std_17)

# 消費上游（已合併，可讀不可改）：
#   e7_01 — Value / FormatVersion / Document（遷移作用的資料模型與版本欄位）
#   e9_02 — 帶版本欄位的 manifest 格式（同屬描述子系統的版本相容策略）
target_link_libraries(e7_08 PUBLIC e7_01 e9_02)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_08 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_08.cpp)
target_link_libraries(test_e7_08 PRIVATE e7_08 GTest::gtest_main)
add_test(NAME e7_08 COMMAND test_e7_08)

# ==== 單元 e7_09 ====
# engine/format/e7_09/CMakeLists.txt — E7-09 缺席模組的降級解析
# 引用缺席 / 未安裝模組時的優雅降級（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_09 STATIC fallback.cpp)
target_include_directories(e7_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_09 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：E7-01 Value 樹 + E9-02 manifest 模組清單。
target_link_libraries(e7_09 PUBLIC e7_01 e9_02)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_09 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_09.cpp)
target_link_libraries(test_e7_09 PRIVATE e7_09 GTest::gtest_main)
add_test(NAME e7_09 COMMAND test_e7_09)

# ==== 單元 e7_10 ====
# engine/format/e7_10/CMakeLists.txt — E7-10 字串處理
# 描述子系統的字串運算函式庫（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_10 STATIC strings.cpp)
target_include_directories(e7_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_10 PUBLIC cxx_std_17)

# 消費 E7-05 的 EvalResult / EvalError（且傳遞取得 E7-02 的 VariableScope、E7-01 的 Value）；
# 上游已合併，可讀不可改。PUBLIC 讓下游一併取得其標頭。
target_link_libraries(e7_10 PUBLIC e7_05)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_10 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_10.cpp)
target_link_libraries(test_e7_10 PRIVATE e7_10 GTest::gtest_main)
add_test(NAME e7_10 COMMAND test_e7_10)

# ==== 單元 e7_11 ====
# engine/format/e7_11/CMakeLists.txt — E7-11 群組與批次操作（engine 層，平台中立）
# 依賴上游 E7-01（ds::format::Value）與 E6-01（ds::command::CommandBus）。
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e7_11 STATIC group.cpp)
target_include_directories(e7_11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_11 PUBLIC cxx_std_17)

# 上游已合併，可讀不可改；PUBLIC 連結使消費者一併取得其 include 目錄。
target_link_libraries(e7_11 PUBLIC e7_01 e6_01)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_11 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_11.cpp)
target_link_libraries(test_e7_11 PRIVATE e7_11 GTest::gtest_main)
add_test(NAME e7_11 COMMAND test_e7_11)

# ==== 單元 e7_12 ====
# engine/format/e7_12/CMakeLists.txt — E7-12 設定值寫回
# E7-01 宣告式格式核心的應用（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_12 STATIC writeback.cpp)
target_include_directories(e7_12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_12 PUBLIC cxx_std_17)

# 相依上游 E7-01（已合併，可讀不可改）：消費其 Value / Document / FormatVersion 契約。
target_link_libraries(e7_12 PUBLIC e7_01)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_12 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_12.cpp)
target_link_libraries(test_e7_12 PRIVATE e7_12 GTest::gtest_main)
add_test(NAME e7_12 COMMAND test_e7_12)

# ==== 單元 e7_13 ====
# engine/format/e7_13/CMakeLists.txt — E7-13 階層式項目結構
# E7-01 宣告式格式核心的應用（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_13 STATIC item_tree.cpp)
target_include_directories(e7_13 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_13 PUBLIC cxx_std_17)

# 相依上游 E7-01（已合併，可讀不可改）：消費其 Value / Document 契約。
target_link_libraries(e7_13 PUBLIC e7_01)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_13 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_13.cpp)
target_link_libraries(test_e7_13 PRIVATE e7_13 GTest::gtest_main)
add_test(NAME e7_13 COMMAND test_e7_13)

# ==== 單元 e7_14 ====
# engine/format/e7_14/CMakeLists.txt — E7-14 圖形化設定與就地編輯
# E7-12 設定值寫回（並經其相依 E7-01）的應用（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_14 STATIC editor_model.cpp)
target_include_directories(e7_14 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_14 PUBLIC cxx_std_17)

# 相依上游 E7-12（已合併，可讀不可改）：消費其 Path / set_value / serialize 契約；
# E7-01 的 Value / Document 經 e7_12 的 PUBLIC 傳遞相依可見。
target_link_libraries(e7_14 PUBLIC e7_12)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_14 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_14.cpp)
target_link_libraries(test_e7_14 PRIVATE e7_14 GTest::gtest_main)
add_test(NAME e7_14 COMMAND test_e7_14)

# ==== 單元 e7_15 ====
# engine/format/e7_15/CMakeLists.txt — E7-15 拖放產生設定項
# E7-12 設定值寫回 + E5-08 系統事件的應用（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_15 STATIC dropgen.cpp)
target_include_directories(e7_15 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_15 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   e7_12 → 設定值寫回（set_value / serialize；並傳遞 e7_01 的 Value / Document）。
#   e5_08 → 系統事件（SystemEvent / SystemEventSource；拖放內容經其事件分派注入）。
target_link_libraries(e7_15 PUBLIC e7_12 e5_08)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_15 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_15.cpp)
target_link_libraries(test_e7_15 PRIVATE e7_15 GTest::gtest_main)
add_test(NAME e7_15 COMMAND test_e7_15)
