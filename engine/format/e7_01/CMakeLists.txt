# engine/format/e7_01/CMakeLists.txt — E7-01 宣告式格式核心 + 版本欄位
# 描述子系統的根契約（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_01 STATIC document.cpp)
target_include_directories(e7_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_01 PUBLIC cxx_std_17)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_01 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_01.cpp)
target_link_libraries(test_e7_01 PRIVATE e7_01 GTest::gtest_main)
add_test(NAME e7_01 COMMAND test_e7_01)
