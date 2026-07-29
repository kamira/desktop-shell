# modules/elements/e4_07/CMakeLists.txt — E4-07 幀序列動畫
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_07 STATIC frame_animation_element.cpp)
target_include_directories(e4_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_07 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_02：圖片元件（ImageElement / ImageSource / MemoryImageSource / ImageRenderModel 等）。
#   - e4_09：動畫驅動源（AnimationDriver / AnimationId / Tick）。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_07 PUBLIC e4_02 e4_09)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_07 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_07.cpp)
target_link_libraries(test_e4_07 PRIVATE e4_07 GTest::gtest_main)
add_test(NAME e4_07 COMMAND test_e4_07)
