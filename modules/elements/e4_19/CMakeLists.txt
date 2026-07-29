# modules/elements/e4_19/CMakeLists.txt — E4-19 旋轉元件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_19 STATIC rotatable_element.cpp)
target_include_directories(e4_19 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_19 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_02：圖片元件（ImageElement / ImageSource / MemoryImageSource / ImageRenderModel 等）。
#   - e4_22：2D 變形矩陣（Transform2D / Vec2），用以計算繞具名 pivot 的旋轉矩陣。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_19 PUBLIC e4_02 e4_22)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_19 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_19.cpp)
target_link_libraries(test_e4_19 PRIVATE e4_19 GTest::gtest_main)
add_test(NAME e4_19 COMMAND test_e4_19)
