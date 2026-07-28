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
