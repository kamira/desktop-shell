# engine/render/e4_08/CMakeLists.txt — E4-08 圖層合成
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e4_08 STATIC layer_compositor.cpp)
target_include_directories(e4_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_08 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_02（圖片元件）：依規格連結；本單元標頭經 e4_06 -> e4_02 的 include 鏈取得
#     ds::kernel::SurfaceId，不使用 ImageElement 本身。
#   - e4_06（Surface 編號定址與切換）：本單元圖層以其 SurfaceSwitcher::has() 做定址存在性
#     檢查，確保合成計畫只描述已知的具名 surface。
# PUBLIC：本單元標頭 include surface_switcher.hpp（其鏈上又 include image_element.hpp），
# 需傳遞兩者的 include 目錄。
target_link_libraries(e4_08 PUBLIC e4_02 e4_06)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_08 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_08.cpp)
target_link_libraries(test_e4_08 PRIVATE e4_08 GTest::gtest_main)
add_test(NAME e4_08 COMMAND test_e4_08)
