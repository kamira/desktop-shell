# engine/events/e5_07/CMakeLists.txt — E5-07 區域內連續拖曳判定
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_07 STATIC region_drag_recognizer.cpp)
target_include_directories(e5_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_07 PUBLIC cxx_std_17)

# 相依上游 E5-14（碰撞區域名事件參數）：本單元內部私有持有一個 e5_14
# `RegionEventDispatcher` 實例，完全委派具名 surface 命中 + 具名子區域查詢。PUBLIC：本單元
# 標頭 include region_event_dispatcher.hpp（需傳遞 include 目錄；e5_14 已 PUBLIC 連
# e5_01 + e1_05，故 MouseButton/MouseAction/RouteStatus/HitSurface/NamedRegionMap 等一併
# 可得）。
target_link_libraries(e5_07 PUBLIC e5_14)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_07 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_07.cpp)
target_link_libraries(test_e5_07 PRIVATE e5_07 GTest::gtest_main)
add_test(NAME e5_07 COMMAND test_e5_07)
