# src/kernel/e1_11/CMakeLists.txt — E1-11 從屬 surface 相對定位
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_11 STATIC subordinate_layout.cpp)
target_include_directories(e1_11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_11 PUBLIC cxx_std_17)

# 相依上游 E1-07（可讀不可改）：承其 Anchor / AnchorSpec / Size / ResolvedPlacement /
# AnchorStatus / resolve()，子 surface 以「父已解析矩形」作為 E1-07 resolve() 的 container
# 解析出相對佈局（NFR-02：具名 anchor + 相對偏移，無絕對座標）。
# PUBLIC：本單元標頭 include anchor_model.hpp（經 e1_07 傳遞 e1_03 / backend_null 的 include 目錄）。
target_link_libraries(e1_11 PUBLIC e1_07)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_11 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_11.cpp)
target_link_libraries(test_e1_11 PRIVATE e1_11 GTest::gtest_main)
add_test(NAME e1_11 COMMAND test_e1_11)
