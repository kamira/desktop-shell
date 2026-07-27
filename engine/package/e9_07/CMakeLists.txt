# engine/package/e9_07/CMakeLists.txt — E9-07 拖放安裝
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_07 STATIC drop_install.cpp)
target_include_directories(e9_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_07 PUBLIC cxx_std_17)

# 相依上游：E9-01（套件格式，PUBLIC 傳遞 package.hpp / 並經其傳遞 E9-02 manifest.hpp）、
# E5-08（系統事件，PUBLIC 傳遞 system_event.hpp）。二者已合併於 main，可讀不可改。
target_link_libraries(e9_07 PUBLIC e9_01 e5_08)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_07 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_07.cpp)
target_link_libraries(test_e9_07 PRIVATE e9_07 GTest::gtest_main)
add_test(NAME e9_07 COMMAND test_e9_07)
