# engine/render/e4_23/CMakeLists.txt — E4-23 容器裁切與遮罩
# 根 CMakeLists 以層級 GLOB（engine/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e4_23 STATIC clip_mask.cpp)
target_include_directories(e4_23 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_23 PUBLIC cxx_std_17)

# 相依上游 E1-03（逐像素 alpha surface / surface 具名指涉慣例，已合併可讀不可改）：
# 本裁切與遮罩服務沿用其 ds::kernel::SurfaceId 具名容器指涉慣例（PUBLIC：本單元標頭
# include null_backend.hpp，需傳遞 include 目錄）。
target_link_libraries(e4_23 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_23 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_23.cpp)
target_link_libraries(test_e4_23 PRIVATE e4_23 GTest::gtest_main)
add_test(NAME e4_23 COMMAND test_e4_23)
