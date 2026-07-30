# content/widgets/c2_07/CMakeLists.txt — C2-07 媒體控制 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_07 STATIC media_control_widget.cpp media_control_button_bridge.cpp)
target_include_directories(c2_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_07 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝五個擴充點——
#   - c1_01（`SkinProfile`）：桌面基底（圖層 / 輸入策略 / 透明外形 / 自由拖曳皆透傳，本單元不
#     重造），PUBLIC 相依 e1_01 e1_02 e1_03 e1_07 e1_08 e7_01。
#   - e2_13（`MediaMetadataProvider` 欄位鍵 / `PlaybackState`）：讀「media.nowplaying」指標，
#     PUBLIC 相依 e2_01（`MetricRegistry` / `Metric`）e2_02。
#   - e3_03（`MediaControlActuator` / `NullMediaControlBackend`）：控制按鈕觸發的注入式後端，
#     PUBLIC 相依 e6_01（命令參數型別）e2_13。
#   - e4_02（`ImageElement` / `MemoryImageSource`）：封面渲染描述，PUBLIC 相依 e1_03。
#   - e4_04（`ButtonElement` / `ButtonRenderModel`）：四顆具名控制按鈕，PUBLIC 相依 e4_02 e1_04
#     e5_02。**只由 `media_control_button_bridge.cpp` #include**——C1-01（PUBLIC 相依 e1_02）
#     與 e4_04（PUBLIC 相依 e1_04）transitively 帶入同名不同型別的 `ds::kernel::HitResult`
#     （E1-02 為 enum class、E1-04 為 struct），若同一翻譯單元同時 #include 兩者標頭會編譯
#     失敗；本單元依 C1-06（`content/profiles/c1_06/dock_hot_zone_bridge.*`）已確立的橋接
#     （pimpl）範式，把 e4_04 的實際 #include 隔離到獨立翻譯單元，見
#     `media_control_button_bridge.hpp` 標頭說明。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_07 PUBLIC c1_01 e2_13 e3_03 e4_02 e4_04)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_07 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_07.cpp)
target_link_libraries(test_c2_07 PRIVATE c2_07 GTest::gtest_main)
add_test(NAME c2_07 COMMAND test_c2_07)
