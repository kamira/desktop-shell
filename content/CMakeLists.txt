# content/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 c3_01 ====
# content/c3_01/CMakeLists.txt — C3-01 啟動器選單樹
# 根 CMakeLists 以 content/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c3_01 STATIC launcher_menu_tree.cpp)
target_include_directories(c3_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c3_01 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：
#   - c1_05（C1-05 召喚面板基底 profile）：本單元掛載其上，重用其選項森林儲存
#     （set_items / items()，本身即透傳 E7-13 build_forest / Item）。
#   - e7_13（階層式項目結構）：經 c1_05 透傳，這裡直接列出以求清楚（Item / Value 直接用於本單元標頭）。
#   - e3_02（啟動程式 / 開檔 / 網頁搜尋致動器）：葉節點選取時透過其具名命令觸發（經其上游
#     e6_01 命令匯流排注入式分派）。
target_link_libraries(c3_01 PUBLIC c1_05 e7_13 e3_02)

# 單元測試在 tests/c3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c3_01 ${CMAKE_SOURCE_DIR}/tests/c3/test_c3_01.cpp)
target_link_libraries(test_c3_01 PRIVATE c3_01 GTest::gtest_main)
add_test(NAME c3_01 COMMAND test_c3_01)

# ==== 單元 c3_02 ====
# content/c3_02/CMakeLists.txt — C3-02 角色對話本
# 根 CMakeLists 以 content/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c3_02 STATIC dialogue_book.cpp)
target_include_directories(c3_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c3_02 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝三個擴充點——
#   - c1_03（依附角色顯示逐句台詞的氣球 profile；PUBLIC 相依 e1_11 / e1_14 / e4_11 / c1_02）
#   - e8_02（對話腳本直譯器：Script / Step / Interpreter，含條件分支 / 跳轉；PUBLIC 相依 e7_05）
#   - e8_03（表現控制指令集：舞台指示 show/hide/switch_surface/transition/wait 經 CommandBus
#     分派；PUBLIC 相依 e8_02 / e4_06 / e6_01）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c3_02 PUBLIC c1_03 e8_02 e8_03)

# 單元測試在 tests/c3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c3_02 ${CMAKE_SOURCE_DIR}/tests/c3/test_c3_02.cpp)
target_link_libraries(test_c3_02 PRIVATE c3_02 GTest::gtest_main)
add_test(NAME c3_02 COMMAND test_c3_02)

# ==== 單元 c3_03 ====
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
