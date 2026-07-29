# engine/events/e5_01/CMakeLists.txt — E5-01 基本滑鼠按鍵事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_01 STATIC mouse_button_input.cpp)
target_include_directories(e5_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_01 PUBLIC cxx_std_17)

# 相依上游 E1-04（幾何命中測試）：命中判定完全委由 HitTester::topmost_hit()。
# PUBLIC：本單元標頭 include hit_test.hpp，需傳遞 include 目錄（e1_04 已 PUBLIC 連
# e1_03 / e1_21 / backend_null，故 LocalPoint / SurfaceId / HitSurface 等一併可得）。
target_link_libraries(e5_01 PUBLIC e1_04)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_01 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_01.cpp)
target_link_libraries(test_e5_01 PRIVATE e5_01 GTest::gtest_main)
add_test(NAME e5_01 COMMAND test_e5_01)
