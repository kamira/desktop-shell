# engine/events/e5_09/CMakeLists.txt — E5-09 機率排程器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_09 STATIC probabilistic_scheduler.cpp)
target_include_directories(e5_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_09 PUBLIC cxx_std_17)

# 相依已合併的 E5-04 心跳（PUBLIC：本單元標頭在介面上使用其 Tick / HeartbeatSource）。
target_link_libraries(e5_09 PUBLIC e5_04)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_09 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_09.cpp)
target_link_libraries(test_e5_09 PRIVATE e5_09 GTest::gtest_main)
add_test(NAME e5_09 COMMAND test_e5_09)
