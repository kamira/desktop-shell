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
