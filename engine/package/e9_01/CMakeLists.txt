# engine/package/e9_01/CMakeLists.txt — E9-01 統一套件格式定義（複用 E9-02 manifest）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_01 STATIC package.cpp)
target_include_directories(e9_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_01 PUBLIC cxx_std_17)

# 相依上游單元 E9-02（manifest）：PUBLIC 傳遞其 include 路徑，package.hpp 得以 #include "manifest.hpp"。
target_link_libraries(e9_01 PUBLIC e9_02)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_01 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_01.cpp)
target_link_libraries(test_e9_01 PRIVATE e9_01 GTest::gtest_main)
add_test(NAME e9_01 COMMAND test_e9_01)
