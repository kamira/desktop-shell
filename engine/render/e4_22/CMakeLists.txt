# engine/render/e4_22/CMakeLists.txt — E4-22 2D 變形矩陣
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e4_22 STATIC transform2d.cpp)
target_include_directories(e4_22 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_22 PUBLIC cxx_std_17)

# 相依上游 E1-03（逐像素 alpha surface / surface 幾何模型，已合併可讀不可改）：
# 本 2D 變形矩陣為供渲染層變形 surface 幾何的數學工具，宣告於 surface 模型之上。
target_link_libraries(e4_22 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_22 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_22.cpp)
target_link_libraries(test_e4_22 PRIVATE e4_22 GTest::gtest_main)
add_test(NAME e4_22 COMMAND test_e4_22)
