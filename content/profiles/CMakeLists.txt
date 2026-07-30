# content/profiles/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 c1_01 ====
# content/profiles/c1_01/CMakeLists.txt — C1-01 Skin profile（桌面寵物 / 角色皮膚基底 profile）
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_01 STATIC skin_profile.cpp)
target_include_directories(c1_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_01 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝六個擴充點——
#   - e1_01（具名圖層列舉與 z-order 維持，PUBLIC 相依 e1_21 + backend_null）
#   - e1_02（輸入策略四態，PUBLIC 相依 e1_21 + backend_null）
#   - e1_03（逐像素 alpha 透明外形，PUBLIC 相依 e1_21 + backend_null）
#   - e1_07（anchor 定位模型，PUBLIC 相依 e1_03）
#   - e1_08（自由拖曳 + 位置記憶，PUBLIC 相依 e1_03 + e1_07 + e7_12）
#   - e7_01（宣告式格式核心：Value 資料模型，供 load_skin 消費）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c1_01 PUBLIC e1_01 e1_02 e1_03 e1_07 e1_08 e7_01)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_01 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_01.cpp)
target_link_libraries(test_c1_01 PRIVATE c1_01 GTest::gtest_main)
add_test(NAME c1_01 COMMAND test_c1_01)

# ==== 單元 c1_02 ====
# content/profiles/c1_02/CMakeLists.txt — C1-02 立繪 profile
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_02 STATIC portrait_profile.cpp)
target_include_directories(c1_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_02 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝六個擴充點——
#   - e1_01（具名圖層列舉與 z-order 維持）+ e1_03（逐像素 alpha surface，經其傳遞 e1_21 /
#     backend_null）+ e1_08（自由拖曳與位置記憶，PUBLIC 相依 e1_03 / e1_07 / e7_12）
#   - e4_06（Surface 編號定址與切換，PUBLIC 相依 e4_02 圖片元件，經其傳遞 e1_03）
#   - e5_14（碰撞區域名事件參數，PUBLIC 相依 e5_01 / e1_05）
#   - e7_01（宣告式格式核心，亦經 e1_08 -> e7_12 透傳）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c1_02 PUBLIC e1_01 e1_03 e1_08 e4_06 e5_14 e7_01)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_02 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_02.cpp)
target_link_libraries(test_c1_02 PRIVATE c1_02 GTest::gtest_main)
add_test(NAME c1_02 COMMAND test_c1_02)

# ==== 單元 c1_03 ====
# content/profiles/c1_03/CMakeLists.txt — C1-03 氣球 profile
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_03 STATIC balloon_profile.cpp)
target_include_directories(c1_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_03 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝三個擴充點——
#   - e1_11（依附角色的具名 anchor 相對定位，PUBLIC 相依 e1_07，經其傳遞 e1_03 / backend_null）
#   - e1_14（短暫生命週期，注入式逾時計時，PUBLIC 相依 e1_02 / e5_10）
#   - e4_11（逐字顯示，PUBLIC 相依 e4_01 / e4_09）
#   - c1_02（依附對象：立繪 profile，PUBLIC 相依 e1_01 / e1_03 / e1_08 / e4_06 / e5_14 / e7_01）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
#
# 原上游命名碰撞已於 CHG-20260730-02 解除：E1-02 的 `HitResult` 改名為 `InputHitResult` 後，
# 不再與 E1-04（經 c1_02 → e5_14 → e5_01 → hit_test.hpp）的 `struct HitResult` 同名衝突。
# 因此 character_bridge.cpp（原用於把 c1_02 串接隔離到獨立翻譯單元）已移除，balloon_profile.cpp
# 直接 `#include "portrait_profile.hpp"` 讀取角色狀態。（E1-14 串接仍以 balloon_profile.cpp 內的
# pimpl 表達，屬實作選擇，非碰撞所需。）
target_link_libraries(c1_03 PUBLIC e1_11 e1_14 e4_11 c1_02)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_03 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_03.cpp)
target_link_libraries(test_c1_03 PRIVATE c1_03 GTest::gtest_main)
add_test(NAME c1_03 COMMAND test_c1_03)

# ==== 單元 c1_04 ====
# content/profiles/c1_04/CMakeLists.txt — C1-04 OSD 浮層 profile
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_04 STATIC osd_overlay_profile.cpp)
target_include_directories(c1_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_04 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝三個擴充點——
#   - e1_01（具名圖層列舉與 z-order 維持，PUBLIC 相依 e1_21 + backend_null）
#   - e1_02（輸入策略四態，PUBLIC 相依 e1_21 + backend_null）
#   - e1_14（短暫 profile 生命週期，PUBLIC 相依 e1_02 + e5_10）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c1_04 PUBLIC e1_01 e1_02 e1_14)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_04 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_04.cpp)
target_link_libraries(test_c1_04 PRIVATE c1_04 GTest::gtest_main)
add_test(NAME c1_04 COMMAND test_c1_04)

# ==== 單元 c1_05 ====
# content/profiles/c1_05/CMakeLists.txt — C1-05 召喚面板 profile
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_05 STATIC summon_panel_profile.cpp)
target_include_directories(c1_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_05 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝四個擴充點——
#   - e1_02（輸入策略四態）+ e1_14（短暫 profile 生命週期，PUBLIC 相依 e1_02）
#   - e5_05（全域熱鍵註冊）
#   - e7_13（階層式項目結構，PUBLIC 相依 e7_01）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c1_05 PUBLIC e1_02 e1_14 e5_05 e7_13)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_05 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_05.cpp)
target_link_libraries(test_c1_05 PRIVATE c1_05 GTest::gtest_main)
add_test(NAME c1_05 COMMAND test_c1_05)

# ==== 單元 c1_06 ====
# content/profiles/c1_06/CMakeLists.txt — C1-06 Dock profile
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_06 STATIC dock_profile.cpp dock_hot_zone_bridge.cpp)
target_include_directories(c1_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_06 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝三個擴充點——
#   - e1_01（具名圖層堆疊，指派本 dock 至 Topmost）
#   - e1_02（輸入策略四態）
#   - e1_16（螢幕邊緣熱區，PUBLIC 相依 e1_04；經本單元 dock_hot_zone_bridge 隔離其
#     transitively 帶入的 E1-04 HitResult 與 e1_02 HitResult 之上游既有同名碰撞——見
#     dock_types.hpp / dock_hot_zone_bridge.hpp 標頭說明）
# PUBLIC：dock_profile.hpp 直接 include e1_01 / e1_02 標頭，故傳遞其 include 目錄；
# e1_16 僅由 dock_hot_zone_bridge.cpp（非對外標頭）include，但仍 PUBLIC 列出以符合本單元
# CMake 樣板規格（BUILD.md）與 target_link_libraries 相依宣告的完整性。
target_link_libraries(c1_06 PUBLIC e1_01 e1_02 e1_16)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_06 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_06.cpp)
target_link_libraries(test_c1_06 PRIVATE c1_06 GTest::gtest_main)
add_test(NAME c1_06 COMMAND test_c1_06)

# ==== 單元 c1_07 ====
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
