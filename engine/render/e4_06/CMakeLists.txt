# engine/render/e4_06/CMakeLists.txt — E4-06 Surface 編號定址與切換
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e4_06 STATIC surface_switcher.cpp)
target_include_directories(e4_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_06 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_02（圖片元件）：本單元僅借道其標頭 image_element.hpp 傳遞 ds::kernel::SurfaceId
#     （經其 include 的 e1_03/alpha_surface.hpp -> backend_null/null_backend.hpp 一路 PUBLIC
#     傳遞的具名 surface 識別碼型別），沿用既有型別、不重造一套 id。
#     PUBLIC：本單元標頭 include image_element.hpp，需傳遞其 include 目錄。
target_link_libraries(e4_06 PUBLIC e4_02)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_06 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_06.cpp)
target_link_libraries(test_e4_06 PRIVATE e4_06 GTest::gtest_main)
add_test(NAME e4_06 COMMAND test_e4_06)
