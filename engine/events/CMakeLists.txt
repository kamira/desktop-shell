# engine/events/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e5_01 ====
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

# ==== 單元 e5_02 ====
# engine/events/e5_02/CMakeLists.txt — E5-02 懸停進出事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_02 STATIC hover_tracker.cpp)
target_include_directories(e5_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_02 PUBLIC cxx_std_17)

# 相依上游：E1-04 幾何命中測試（HitTester / HitSurface / LocalPoint / TopmostHit）。
# PUBLIC：本單元標頭 include hit_test.hpp，需傳遞其 include 目錄（e1_04 已 PUBLIC 連 e1_03，
# 故 alpha_surface.hpp / null_backend.hpp 的 include 目錄亦傳遞可得）。
target_link_libraries(e5_02 PUBLIC e1_04)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_02 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_02.cpp)
target_link_libraries(test_e5_02 PRIVATE e5_02 GTest::gtest_main)
add_test(NAME e5_02 COMMAND test_e5_02)

# ==== 單元 e5_03 ====
# engine/events/e5_03/CMakeLists.txt — E5-03 生命週期事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_03 STATIC lifecycle_source.cpp)
target_include_directories(e5_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_03 PUBLIC cxx_std_17)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_03 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_03.cpp)
target_link_libraries(test_e5_03 PRIVATE e5_03 GTest::gtest_main)
add_test(NAME e5_03 COMMAND test_e5_03)

# ==== 單元 e5_04 ====
# engine/events/e5_04/CMakeLists.txt — E5-04 心跳事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_04 STATIC heartbeat_source.cpp)
target_include_directories(e5_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_04 PUBLIC cxx_std_17)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_04 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_04.cpp)
target_link_libraries(test_e5_04 PRIVATE e5_04 GTest::gtest_main)
add_test(NAME e5_04 COMMAND test_e5_04)

# ==== 單元 e5_06 ====
# engine/events/e5_06/CMakeLists.txt — E5-06 滾輪事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_06 STATIC wheel_event_input.cpp)
target_include_directories(e5_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_06 PUBLIC cxx_std_17)

# 相依上游 E1-04（幾何命中測試）：命中判定完全委由 HitTester::topmost_hit()。
# PUBLIC：本單元標頭 include hit_test.hpp，需傳遞 include 目錄（e1_04 已 PUBLIC 連
# e1_03 / e1_21 / backend_null，故 LocalPoint / SurfaceId / HitSurface 等一併可得）。
target_link_libraries(e5_06 PUBLIC e1_04)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_06 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_06.cpp)
target_link_libraries(test_e5_06 PRIVATE e5_06 GTest::gtest_main)
add_test(NAME e5_06 COMMAND test_e5_06)

# ==== 單元 e5_07 ====
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

# ==== 單元 e5_09 ====
# engine/events/e5_09/CMakeLists.txt — E5-09 機率排程器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_09 STATIC probabilistic_scheduler.cpp)
target_include_directories(e5_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_09 PUBLIC cxx_std_17)

# 相依已合併的 E5-04 心跳（PUBLIC：本單元標頭在介面上使用其 Tick / HeartbeatSource）。
target_link_libraries(e5_09 PUBLIC e5_04)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_09 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_09.cpp)
target_link_libraries(test_e5_09 PRIVATE e5_09 GTest::gtest_main)
add_test(NAME e5_09 COMMAND test_e5_09)

# ==== 單元 e5_10 ====
# engine/events/e5_10/CMakeLists.txt — E5-10 逾時計時器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_10 STATIC timeout_timer.cpp)
target_include_directories(e5_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_10 PUBLIC cxx_std_17)

# 相依上游 E5-04 心跳（已合併，可讀不可改）：沿用其 ds::events::Tick 型別。
target_link_libraries(e5_10 PUBLIC e5_04)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_10 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_10.cpp)
target_link_libraries(test_e5_10 PRIVATE e5_10 GTest::gtest_main)
add_test(NAME e5_10 COMMAND test_e5_10)

# ==== 單元 e5_11 ====
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

# ==== 單元 e5_13 ====
# engine/events/e5_13/CMakeLists.txt — E5-13 鍵盤與輸入提交事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_13 STATIC keyboard_input.cpp)
target_include_directories(e5_13 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_13 PUBLIC cxx_std_17)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_13 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_13.cpp)
target_link_libraries(test_e5_13 PRIVATE e5_13 GTest::gtest_main)
add_test(NAME e5_13 COMMAND test_e5_13)

# ==== 單元 e5_14 ====
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
