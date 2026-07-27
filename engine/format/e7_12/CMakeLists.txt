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
