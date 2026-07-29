# modules/elements/e4_29/CMakeLists.txt — E4-29 折線圖
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_29 STATIC line_chart.cpp)
target_include_directories(e4_29 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_29 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_17（直方圖）：沿用其 HistogramRange / HistogramStatus 型別（y 軸值域 / 操作結果碼），
#     並經其 PUBLIC 連結傳遞 e1_03（AlphaProfile 合成描述）的 include 目錄。
# PUBLIC：本單元標頭 include histogram.hpp（進而 alpha_surface.hpp），需傳遞其 include 目錄。
target_link_libraries(e4_29 PUBLIC e4_17)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_29 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_29.cpp)
target_link_libraries(test_e4_29 PRIVATE e4_29 GTest::gtest_main)
add_test(NAME e4_29 COMMAND test_e4_29)
