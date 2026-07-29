# src/kernel/e1_06/CMakeLists.txt — E1-06 命中與繪製層序
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_06 STATIC paint_hit_order.cpp)
target_include_directories(e1_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_06 PUBLIC cxx_std_17)

# 相依上游 E1-04（幾何命中測試）：本單元標頭 include hit_test.hpp，且完全複用其
# HitTester::hit_test_alpha 做幾何 / alpha 判定，不重新實作。PUBLIC：需傳遞 include 目錄
# （e1_04 已 PUBLIC 連 e1_03，故 alpha_surface.hpp / null_backend.hpp 之 include 目錄亦傳遞可得）。
target_link_libraries(e1_06 PUBLIC e1_04)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_06 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_06.cpp)
target_link_libraries(test_e1_06 PRIVATE e1_06 GTest::gtest_main)
add_test(NAME e1_06 COMMAND test_e1_06)
