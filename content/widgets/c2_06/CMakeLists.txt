# content/widgets/c2_06/CMakeLists.txt — C2-06 動畫 / 序列圖 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_06 STATIC animation_widget.cpp)
target_include_directories(c2_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_06 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元是組裝型 widget——
#   - c1_01（`ds::profiles::SkinProfile`）：widget 掛載的基底（渲染輸出綁定其具名 surface id）。
#   - e4_07（`ds::elements::FrameAnimationElement`）：幀序列動畫本體（configure/play/pause/
#     advance/current_frame/render_model 皆為其薄封裝）。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_06 PUBLIC c1_01 e4_07)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_06 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_06.cpp)
target_link_libraries(test_c2_06 PRIVATE c2_06 GTest::gtest_main)
add_test(NAME c2_06 COMMAND test_c2_06)
