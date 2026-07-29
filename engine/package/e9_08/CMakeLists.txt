# engine/package/e9_08/CMakeLists.txt — E9-08 圖形安裝器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_08 STATIC install_wizard.cpp)
target_include_directories(e9_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_08 PUBLIC cxx_std_17)

# 相依上游：E9-07（拖放安裝，PUBLIC 傳遞 drop_install.hpp，並經其傳遞 E9-01 / E9-02 / E5-08）。
# 已合併於 main，可讀不可改。
target_link_libraries(e9_08 PUBLIC e9_07)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_08 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_08.cpp)
target_link_libraries(test_e9_08 PRIVATE e9_08 GTest::gtest_main)
add_test(NAME e9_08 COMMAND test_e9_08)
