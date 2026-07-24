# src/events/e5_08/CMakeLists.txt — E5-08 系統事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_08 STATIC system_event.cpp)
target_include_directories(e5_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_08 PUBLIC cxx_std_17)

# 契約測試在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_08 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_08.cpp)
target_link_libraries(test_e5_08 PRIVATE e5_08 GTest::gtest_main)
add_test(NAME e5_08 COMMAND test_e5_08)
