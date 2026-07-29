# src/kernel/e1_04/CMakeLists.txt — E1-04 幾何命中測試
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_04 STATIC hit_test.cpp)
target_include_directories(e1_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_04 PUBLIC cxx_std_17)

# 相依上游：
#   - e1_03（逐像素 alpha surface）：命中測試需其 AlphaMode / AlphaProfile 表達 surface 的 alpha
#     命中語意（透明處不命中）。PUBLIC：本單元標頭 include alpha_surface.hpp，需傳遞 include 目錄。
#     （e1_03 已 PUBLIC 連 e1_21 + backend_null，故 null_backend.hpp 的 SurfaceId / SurfaceLayer /
#     HitPolicy 與 capability_matrix.hpp 之 include 目錄亦傳遞可得。）
target_link_libraries(e1_04 PUBLIC e1_03)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_04 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_04.cpp)
target_link_libraries(test_e1_04 PRIVATE e1_04 GTest::gtest_main)
add_test(NAME e1_04 COMMAND test_e1_04)
