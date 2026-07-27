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
