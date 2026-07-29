# modules/elements/e4_18/CMakeLists.txt — E4-18 徑向線 / 指針
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_18 STATIC radial_pointer.cpp)
target_include_directories(e4_18 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_18 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03：以具名 SurfaceId 指涉元件所在 surface（其標頭 alpha_surface.hpp 於本單元標頭 include）。
#   - e7_03：以宣告式「變數段落」文件驅動設定（section_vars.hpp 的 Value / Document / expand）。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_18 PUBLIC e1_03 e7_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_18 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_18.cpp)
target_link_libraries(test_e4_18 PRIVATE e4_18 GTest::gtest_main)
add_test(NAME e4_18 COMMAND test_e4_18)
