# src/host/e11_01/CMakeLists.txt — E11-01 系統匣圖示與右鍵選單
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e11_01 STATIC tray.cpp)
target_include_directories(e11_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e11_01 PUBLIC cxx_std_17)

# 依賴 E6-01 命令匯流排（上游，已合併，可讀不可改）；PUBLIC 以傳遞其標頭包含路徑。
target_link_libraries(e11_01 PUBLIC e6_01)

# 契約測試在 tests/e11/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e11_01 ${CMAKE_SOURCE_DIR}/tests/e11/test_e11_01.cpp)
target_link_libraries(test_e11_01 PRIVATE e11_01 GTest::gtest_main)
add_test(NAME e11_01 COMMAND test_e11_01)
