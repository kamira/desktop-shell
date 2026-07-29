# modules/elements/e4_28/CMakeLists.txt — E4-28 透視與景深呈現
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_28 STATIC perspective_element.cpp)
target_include_directories(e4_28 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_28 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_22：2D 變形矩陣（Transform2D / Vec2），用以計算依相對深度換算的透視變形（縮放 +
#     朝具名消失點方向的視差位移）。
# PUBLIC：本單元標頭 include transform2d.hpp，需傳遞其 include 目錄給下游。
target_link_libraries(e4_28 PUBLIC e4_22)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_28 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_28.cpp)
target_link_libraries(test_e4_28 PRIVATE e4_28 GTest::gtest_main)
add_test(NAME e4_28 COMMAND test_e4_28)
