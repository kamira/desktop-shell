# engine/package/e9_06/CMakeLists.txt — E9-06 套件卸載流程
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_06 STATIC uninstall.cpp)
target_include_directories(e9_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_06 PUBLIC cxx_std_17)

# 相依上游：E9-01（套件格式，PUBLIC 傳遞 package.hpp / 並經其傳遞 E9-02 manifest.hpp）。
# 已合併於 main，可讀不可改。
target_link_libraries(e9_06 PUBLIC e9_01)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_06 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_06.cpp)
target_link_libraries(test_e9_06 PRIVATE e9_06 GTest::gtest_main)
add_test(NAME e9_06 COMMAND test_e9_06)
