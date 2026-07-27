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
