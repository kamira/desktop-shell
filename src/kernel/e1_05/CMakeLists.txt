# src/kernel/e1_05/CMakeLists.txt — E1-05 具名碰撞區域 → 事件參數
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_05 STATIC named_region_map.cpp)
target_include_directories(e1_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_05 PUBLIC cxx_std_17)

# 相依上游：
#   - e1_04（幾何命中測試）：具名區域的命中判定建於其 HitTester / Shape / LocalPoint 之上。
#     PUBLIC：本單元標頭 include hit_test.hpp，需傳遞 include 目錄（連帶 e1_04 已 PUBLIC 連的
#     e1_03 / e1_21 / backend_null include 目錄亦傳遞可得）。
target_link_libraries(e1_05 PUBLIC e1_04)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_05 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_05.cpp)
target_link_libraries(test_e1_05 PRIVATE e1_05 GTest::gtest_main)
add_test(NAME e1_05 COMMAND test_e1_05)
