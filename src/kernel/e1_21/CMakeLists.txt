# src/kernel/e1_21/CMakeLists.txt — E1-21 能力矩陣宣告檔
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_21 STATIC capability_matrix.cpp)
target_include_directories(e1_21 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_21 PUBLIC cxx_std_17)

# 契約測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_21 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_21.cpp)
target_link_libraries(test_e1_21 PRIVATE e1_21 GTest::gtest_main)
add_test(NAME e1_21 COMMAND test_e1_21)
