# engine/render/e4_01/CMakeLists.txt — E4-01 文字排版與渲染
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，engine/*/*/CMakeLists.txt）。
add_library(e4_01 STATIC text_layout.cpp)
target_include_directories(e4_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_01 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03：以具名 SurfaceId 指涉排版目標 surface（其標頭 alpha_surface.hpp 於本單元標頭 include）。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_01 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_01 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_01.cpp)
target_link_libraries(test_e4_01 PRIVATE e4_01 GTest::gtest_main)
add_test(NAME e4_01 COMMAND test_e4_01)
