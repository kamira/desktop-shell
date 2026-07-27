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
