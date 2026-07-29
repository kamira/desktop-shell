# modules/elements/e4_17/CMakeLists.txt — E4-17 直方圖
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_17 STATIC histogram.cpp)
target_include_directories(e4_17 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_17 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03（逐像素 alpha surface）：合成描述沿用其 AlphaProfile / AlphaMode（半透明 widget 合成）。
#   - e7_03（段落變數）：宣告式設定驅動（configure_from_document 先以其 expand() 展開變數）。
# PUBLIC：本單元標頭 include alpha_surface.hpp 與 section_vars.hpp，需傳遞其 include 目錄。
target_link_libraries(e4_17 PUBLIC e1_03 e7_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_17 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_17.cpp)
target_link_libraries(test_e4_17 PRIVATE e4_17 GTest::gtest_main)
add_test(NAME e4_17 COMMAND test_e4_17)
