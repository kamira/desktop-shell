# engine/package/e9_05/CMakeLists.txt — E9-05 佈局存檔與還原
# 桌面元件佈局的具名存檔 / 還原（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e9_05 STATIC layout_store.cpp)
target_include_directories(e9_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_05 PUBLIC cxx_std_17)

# 相依上游 E7-12 設定值寫回（serialize）；E7-01（Value / Document / parse）經其 PUBLIC 傳遞。
target_link_libraries(e9_05 PUBLIC e7_12)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_05 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_05.cpp)
target_link_libraries(test_e9_05 PRIVATE e9_05 GTest::gtest_main)
add_test(NAME e9_05 COMMAND test_e9_05)
