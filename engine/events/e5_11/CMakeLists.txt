# engine/events/e5_11/CMakeLists.txt — E5-11 可點區段選取事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_11 STATIC span_selection_dispatcher.cpp)
target_include_directories(e5_11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_11 PUBLIC cxx_std_17)

# 相依上游：
#   - e4_12（可點文字區段）：以其 `ClickableTextElement::hit_span()` 判定點擊落在哪個區段。
#     PUBLIC：本單元標頭 include clickable_text.hpp，需傳遞 include 目錄（e4_12 已 PUBLIC 連
#     e4_01 + e1_04，故 TextLayout / HitTester / LocalPoint / SurfaceId 等一併可得）。
#   - e5_01（基本滑鼠按鍵事件）：本單元內部持有一個 e5_01 `MouseEventRouter`，完全委派
#     surface 命中 / 多擊追蹤 / 底層訂閱派發。PUBLIC：本單元標頭 include mouse_button_input.hpp。
target_link_libraries(e5_11 PUBLIC e4_12 e5_01)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_11 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_11.cpp)
target_link_libraries(test_e5_11 PRIVATE e5_11 GTest::gtest_main)
add_test(NAME e5_11 COMMAND test_e5_11)
