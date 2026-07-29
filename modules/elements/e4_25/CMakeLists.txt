# modules/elements/e4_25/CMakeLists.txt — E4-25 背景模式
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_25 STATIC background_element.cpp)
target_include_directories(e4_25 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_25 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03（逐像素 alpha surface）：透明度沿用其 AlphaProfile；並透過其標頭傳遞
#     ds::kernel::SurfaceId（具名目標 surface）。
#   - e4_02（圖片元件）：圖片背景直接重用其 ImageElement / ImageSource / ImageRenderModel，
#     本單元不重造圖片解碼 / 縮放 / 裁切邏輯。
# PUBLIC：本單元標頭 include 兩者標頭，需傳遞其 include 目錄。
target_link_libraries(e4_25 PUBLIC e1_03 e4_02)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_25 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_25.cpp)
target_link_libraries(test_e4_25 PRIVATE e4_25 GTest::gtest_main)
add_test(NAME e4_25 COMMAND test_e4_25)
