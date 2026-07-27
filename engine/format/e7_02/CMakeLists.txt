# engine/format/e7_02/CMakeLists.txt — E7-02 變數系統
# 描述子系統的變數層（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_02 STATIC variables.cpp)
target_include_directories(e7_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_02 PUBLIC cxx_std_17)

# 消費 E7-01 的 Value / Document（已合併，可讀不可改）。PUBLIC 讓下游一併取得其標頭。
target_link_libraries(e7_02 PUBLIC e7_01)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_02 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_02.cpp)
target_link_libraries(test_e7_02 PRIVATE e7_02 GTest::gtest_main)
add_test(NAME e7_02 COMMAND test_e7_02)
