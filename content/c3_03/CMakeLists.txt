# content/c3_03/CMakeLists.txt — C3-03 角色外觀組
# 根 CMakeLists 以 content/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c3_03 STATIC appearance_set.cpp)
target_include_directories(c3_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c3_03 PUBLIC cxx_std_17)

# 上游相依（皆已合併,可讀不可改）：本單元組裝四個擴充點——
#   - c1_02（C1-02 立繪 profile）：本單元的掛載目標，apply_to() 把目前外觀套用到已載入的
#     PortraitProfile。
#   - e1_05（具名碰撞區域 → 事件參數）：每個具名外觀各自登記一組互動區域（NamedRegionMap）。
#   - e4_06（Surface 編號定址與切換）：具名外觀本身以其 SurfaceSwitcher 管理註冊 / 切換。
#   - e9_01（統一套件格式定義）：外觀組的宣告式定義即一份 E9-01 套件描述文字（manifest +
#     內容清單，kind == "look" 的項目即外觀名）。
target_link_libraries(c3_03 PUBLIC c1_02 e1_05 e4_06 e9_01)

# 單元測試在 tests/c3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c3_03 ${CMAKE_SOURCE_DIR}/tests/c3/test_c3_03.cpp)
target_link_libraries(test_c3_03 PRIVATE c3_03 GTest::gtest_main)
add_test(NAME c3_03 COMMAND test_c3_03)
