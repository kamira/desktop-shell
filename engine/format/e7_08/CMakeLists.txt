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
