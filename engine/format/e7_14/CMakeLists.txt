# engine/format/e7_14/CMakeLists.txt — E7-14 圖形化設定與就地編輯
# E7-12 設定值寫回（並經其相依 E7-01）的應用（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_14 STATIC editor_model.cpp)
target_include_directories(e7_14 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_14 PUBLIC cxx_std_17)

# 相依上游 E7-12（已合併，可讀不可改）：消費其 Path / set_value / serialize 契約；
# E7-01 的 Value / Document 經 e7_12 的 PUBLIC 傳遞相依可見。
target_link_libraries(e7_14 PUBLIC e7_12)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_14 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_14.cpp)
target_link_libraries(test_e7_14 PRIVATE e7_14 GTest::gtest_main)
add_test(NAME e7_14 COMMAND test_e7_14)
