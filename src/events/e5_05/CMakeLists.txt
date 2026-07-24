# src/events/e5_05/CMakeLists.txt — E5-05 全域熱鍵註冊
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_05 STATIC global_hotkey.cpp)
target_include_directories(e5_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_05 PUBLIC cxx_std_17)

# 契約測試在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_05 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_05.cpp)
target_link_libraries(test_e5_05 PRIVATE e5_05 GTest::gtest_main)
add_test(NAME e5_05 COMMAND test_e5_05)
