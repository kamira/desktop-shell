# src/kernel/e1_07/CMakeLists.txt — E1-07 anchor 定位模型
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_07 STATIC anchor_model.cpp)
target_include_directories(e1_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_07 PUBLIC cxx_std_17)

# 相依上游 E1-03（可讀不可改）：承其 / E1-24 的具名 surface 模型（SurfaceId），
# AnchorLayout 以具名 SurfaceId 為鍵配對定位規格（NFR-02 具名指涉）。
# PUBLIC：本單元標頭 include null_backend.hpp（經 e1_03 傳遞 backend_null / e1_21 的 include 目錄）。
target_link_libraries(e1_07 PUBLIC e1_03)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_07 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_07.cpp)
target_link_libraries(test_e1_07 PRIVATE e1_07 GTest::gtest_main)
add_test(NAME e1_07 COMMAND test_e1_07)
