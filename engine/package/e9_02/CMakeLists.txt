# engine/package/e9_02/CMakeLists.txt — E9-02 manifest（requires/permissions）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_02 STATIC manifest.cpp)
target_include_directories(e9_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_02 PUBLIC cxx_std_17)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_02 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_02.cpp)
target_link_libraries(test_e9_02 PRIVATE e9_02 GTest::gtest_main)
add_test(NAME e9_02 COMMAND test_e9_02)
