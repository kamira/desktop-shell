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
