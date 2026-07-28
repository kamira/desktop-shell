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
