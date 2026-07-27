# engine/events/e5_10/CMakeLists.txt — E5-10 逾時計時器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_10 STATIC timeout_timer.cpp)
target_include_directories(e5_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_10 PUBLIC cxx_std_17)

# 相依上游 E5-04 心跳（已合併，可讀不可改）：沿用其 ds::events::Tick 型別。
target_link_libraries(e5_10 PUBLIC e5_04)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_10 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_10.cpp)
target_link_libraries(test_e5_10 PRIVATE e5_10 GTest::gtest_main)
add_test(NAME e5_10 COMMAND test_e5_10)
