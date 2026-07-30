# content/profiles/c1_03/CMakeLists.txt — C1-03 氣球 profile
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c1_03 STATIC balloon_profile.cpp character_bridge.cpp)
target_include_directories(c1_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c1_03 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝三個擴充點——
#   - e1_11（依附角色的具名 anchor 相對定位，PUBLIC 相依 e1_07，經其傳遞 e1_03 / backend_null）
#   - e1_14（短暫生命週期，注入式逾時計時，PUBLIC 相依 e1_02 / e5_10）
#   - e4_11（逐字顯示，PUBLIC 相依 e4_01 / e4_09）
#   - c1_02（依附對象：立繪 profile，PUBLIC 相依 e1_01 / e1_03 / e1_08 / e4_06 / e5_14 / e7_01）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
#
# 既有上游命名碰撞（不可修改上游解決，見 balloon_profile.hpp / character_bridge.hpp 說明，
# 同 content/profiles/c1_06 已記錄之相同手法）：e1_14 經 input_strategy.hpp（E1-02）宣告
# `enum class ds::kernel::HitResult`，c1_02 經 e5_14 -> e5_01 -> hit_test.hpp（E1-04）宣告
# 另一個不同型別的 `struct ds::kernel::HitResult`——兩者標頭不可同時出現在同一翻譯單元。
# character_bridge.cpp 把 c1_02 的實際串接隔離到獨立翻譯單元，balloon_profile.cpp 則以 pimpl
# 隔離 e1_14 的實際串接，故 add_library 需列出兩個 .cpp。
target_link_libraries(c1_03 PUBLIC e1_11 e1_14 e4_11 c1_02)

# 單元測試在 tests/c1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c1_03 ${CMAKE_SOURCE_DIR}/tests/c1/test_c1_03.cpp)
target_link_libraries(test_c1_03 PRIVATE c1_03 GTest::gtest_main)
add_test(NAME c1_03 COMMAND test_c1_03)
