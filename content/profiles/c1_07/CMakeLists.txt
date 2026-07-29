# content/profiles/c1_07/CMakeLists.txt — C1-07 全螢幕調光層 profile
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_07 STATIC dim_overlay_profile.cpp)
target_include_directories(c1_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_07 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝一個擴充點——
#   - e4_30（全螢幕調光覆蓋，PUBLIC 傳遞相依 e1_03 per-pixel alpha surface + e1_01 具名圖層堆疊）
# 標頭直接 include 其上游標頭（dim_overlay.hpp / alpha_surface.hpp / layer_stack.hpp / null_backend.hpp），
# 故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c1_07 PUBLIC e4_30 e1_01)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_07 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_07.cpp)
target_link_libraries(test_c1_07 PRIVATE c1_07 GTest::gtest_main)
add_test(NAME c1_07 COMMAND test_c1_07)
