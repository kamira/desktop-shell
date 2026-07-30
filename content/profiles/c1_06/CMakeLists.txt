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
