# engine/package/e9_03/CMakeLists.txt — E9-03 可互換元件組合（建於 E9-01 元件模型之上）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_03 STATIC composition.cpp)
target_include_directories(e9_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_03 PUBLIC cxx_std_17)

# 相依上游單元 E9-01（套件/元件模型）：PUBLIC 傳遞其 include 路徑，
# composition.hpp 得以 #include "package.hpp"（並經 E9-01 再傳遞 E9-02 的 manifest.hpp）。
target_link_libraries(e9_03 PUBLIC e9_01)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_03 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_03.cpp)
target_link_libraries(test_e9_03 PRIVATE e9_03 GTest::gtest_main)
add_test(NAME e9_03 COMMAND test_e9_03)
