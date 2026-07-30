# content/widgets/c2_05/CMakeLists.txt — C2-05 圖片 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_05 STATIC image_widget.cpp)
target_include_directories(c2_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_05 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元是組裝型 widget——
#   - c1_01（`ds::profiles::SkinProfile`）：widget 掛載的基底（渲染輸出綁定其具名 surface id）。
#   - e4_02（`ds::elements::ImageElement`）：單張圖片本體（configure/set_image/render_model
#     皆環繞其 set_source/set_scale_mode/render_model 的薄封裝；widget 層另維護多圖清單）。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_05 PUBLIC c1_01 e4_02)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_05 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_05.cpp)
target_link_libraries(test_c2_05 PRIVATE c2_05 GTest::gtest_main)
add_test(NAME c2_05 COMMAND test_c2_05)
