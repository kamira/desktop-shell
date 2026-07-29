# src/kernel/e1_03/CMakeLists.txt — E1-03 逐像素 alpha surface
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_03 STATIC alpha_surface.cpp)
target_include_directories(e1_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_03 PUBLIC cxx_std_17)

# 相依上游：
#   - e1_21（能力矩陣）：per-pixel alpha 能力宣告 / has() 閘控的資料來源（NFR-03）。
#   - backend_null（E1-24）：提供 kernel 後端抽象介面 KernelBackend + SurfaceId / SurfaceProfile
#     surface 模型；本服務建於其上。PUBLIC：本單元標頭 include null_backend.hpp，需傳遞 include 目錄。
#     （backend_null 已 PUBLIC 連 e1_21，故 e1_21 的 include 目錄亦傳遞可得。）
target_link_libraries(e1_03 PUBLIC e1_21 backend_null)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_03 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_03.cpp)
target_link_libraries(test_e1_03 PRIVATE e1_03 GTest::gtest_main)
add_test(NAME e1_03 COMMAND test_e1_03)
