# src/kernel/e1_16/CMakeLists.txt — E1-16 螢幕邊緣熱區
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_16 STATIC edge_hot_zone.cpp)
target_include_directories(e1_16 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_16 PUBLIC cxx_std_17)

# 相依上游：
#   - e1_04（幾何命中測試）：熱區矩形範圍的點內判定用其 HitTester / Shape / LocalPoint。
#     PUBLIC：本單元標頭 include hit_test.hpp，需傳遞 include 目錄。
target_link_libraries(e1_16 PUBLIC e1_04)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_16 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_16.cpp)
target_link_libraries(test_e1_16 PRIVATE e1_16 GTest::gtest_main)
add_test(NAME e1_16 COMMAND test_e1_16)
