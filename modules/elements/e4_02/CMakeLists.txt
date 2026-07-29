# modules/elements/e4_02/CMakeLists.txt — E4-02 圖片元件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_02 STATIC image_element.cpp)
target_include_directories(e4_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_02 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03（逐像素 alpha surface）：透明度沿用其 AlphaProfile；並透過其標頭傳遞
#     ds::kernel::SurfaceId（具名目標 surface）。
# PUBLIC：本單元標頭 include alpha_surface.hpp，需傳遞其 include 目錄。
target_link_libraries(e4_02 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_02 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_02.cpp)
target_link_libraries(test_e4_02 PRIVATE e4_02 GTest::gtest_main)
add_test(NAME e4_02 COMMAND test_e4_02)
