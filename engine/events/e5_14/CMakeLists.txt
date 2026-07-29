# engine/events/e5_14/CMakeLists.txt — E5-14 碰撞區域名事件參數
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_14 STATIC region_event_dispatcher.cpp)
target_include_directories(e5_14 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_14 PUBLIC cxx_std_17)

# 相依上游：
#   - e5_01（基本滑鼠按鍵事件）：本單元內部持有一個 e5_01 `MouseEventRouter`，完全委派
#     surface 命中 / 多擊追蹤 / 底層訂閱派發。PUBLIC：本單元標頭 include
#     mouse_button_input.hpp，需傳遞 include 目錄（e5_01 已 PUBLIC 連 e1_04，故 LocalPoint /
#     SurfaceId / HitSurface 等一併可得）。
#   - e1_05（具名碰撞區域 → 事件參數）：以其 `NamedRegionMap` 做子區域命中查詢，把具名 id +
#     參數併入橋接後的事件。PUBLIC：本單元標頭 include named_region_map.hpp。
target_link_libraries(e5_14 PUBLIC e5_01 e1_05)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_14 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_14.cpp)
target_link_libraries(test_e5_14 PRIVATE e5_14 GTest::gtest_main)
add_test(NAME e5_14 COMMAND test_e5_14)
