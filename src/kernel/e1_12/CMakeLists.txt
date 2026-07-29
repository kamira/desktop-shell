# src/kernel/e1_12/CMakeLists.txt — E1-12 多重從屬 surface 並存
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_12 STATIC multi_subordinate_manager.cpp)
target_include_directories(e1_12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_12 PUBLIC cxx_std_17)

# 相依上游 E1-11（可讀不可改）：承其 SubordinateLayout（父子附著 + 相對定位），本單元以
# 組合方式持有一個 SubordinateLayout 實例，加一層「多子並存」的列舉 / 批次操作管理。
# PUBLIC：本單元標頭 include subordinate_layout.hpp（經 e1_11 傳遞 e1_07 / e1_03 / e1_21 /
# backend_null 的 include 目錄）。
target_link_libraries(e1_12 PUBLIC e1_11)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_12 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_12.cpp)
target_link_libraries(test_e1_12 PRIVATE e1_12 GTest::gtest_main)
add_test(NAME e1_12 COMMAND test_e1_12)
