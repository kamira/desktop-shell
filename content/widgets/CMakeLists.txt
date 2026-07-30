# content/widgets/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 c2_01 ====
# content/widgets/c2_01/CMakeLists.txt — C2-01 時鐘 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_01 STATIC clock_widget.cpp)
target_include_directories(c2_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_01 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝四個擴充點——
#   - c1_01（掛載基底：桌面角色皮膚 profile，本 widget 綁定其具名 surface id）
#   - e2_10（時間指標：注入式 TimeSource / CivilTime 純算術分解，無真實壁鐘）
#   - e4_01（文字排版：注入式 FontMetrics，排版為相對佈局 LayoutResult）
#   - e7_01（宣告式格式核心：Value 資料模型，供 configure(Value) 消費）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_01 PUBLIC c1_01 e2_10 e4_01 e7_01)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_01 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_01.cpp)
target_link_libraries(test_c2_01 PRIVATE c2_01 GTest::gtest_main)
add_test(NAME c2_01 COMMAND test_c2_01)

# ==== 單元 c2_02 ====
# content/widgets/c2_02/CMakeLists.txt — C2-02 系統狀態 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_02 STATIC system_status_widget.cpp)
target_include_directories(c2_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_02 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝七個擴充點——
#   - c1_01（C1-01 桌面角色皮膚基底 profile）：作為本 widget 的桌面基底，圖層 / 輸入策略 /
#     透明外形 / 自由拖曳皆透傳，本單元不重造。
#   - e2_01（統一指標介面）：唯一的指標讀取入口——消費者只依賴 MetricRegistry::get() /
#     Metric / MetricInstance 抽象契約，不寫死任何具體感測器（CPU/GPU/RAM/…皆可選）。
#   - e2_02（採集頻率分級與除頻）：sync_demands() 依各指標的宣告式 tier 對 SamplingScheduler
#     登記取樣需求（除頻合併）。
#   - e4_01（文字排版與渲染）：把每筆「標籤: 格式化值」文字排版為渲染描述（TextLayout）。
#   - e4_03（長條 / 進度 / 量表）：把指標值依範圍組裝為 Bar/Gauge/Progress 渲染描述。
#   - e7_03（段落變數）：build_scope() 由宣告式設定的 vars: 段落建立具名常數作用域。
#   - e7_05（公式運算引擎）：min/max 靜態公式 + 每指標 transform 動態公式（value 變數綁定）求值。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_02 PUBLIC c1_01 e2_01 e2_02 e4_01 e4_03 e7_03 e7_05)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_02 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_02.cpp)
target_link_libraries(test_c2_02 PRIVATE c2_02 GTest::gtest_main)
add_test(NAME c2_02 COMMAND test_c2_02)

# ==== 單元 c2_04 ====
# content/widgets/c2_04/CMakeLists.txt — C2-04 天氣 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_04 STATIC weather_widget.cpp)
target_include_directories(c2_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_04 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝五個擴充點——
#   - c1_01（C1-01 桌面角色皮膚基底 profile）：作為本 widget 的桌面基底，圖層 / 輸入策略 /
#     透明外形 / 自由拖曳皆透傳，本單元不重造。
#   - e2_14（HTTP 取得與結構化解析）：注入式 HttpTransport 擷取天氣端點 + 自帶 JSON 解析
#     （JsonValue / parse_json / JsonPath / seek）。
#   - e4_01（文字排版與渲染）：把格式化後的溫度 / 描述文字排版為渲染描述（TextLayout）。
#   - e4_02（圖片元件）：把天氣圖示代碼組裝為圖片渲染描述（ImageElement / MemoryImageSource）。
#   - e7_10（字串處理）：以 format() 樣板組出可讀顯示文字、以 concat() 組請求 URL。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_04 PUBLIC c1_01 e2_14 e4_01 e4_02 e7_10)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_04 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_04.cpp)
target_link_libraries(test_c2_04 PRIVATE c2_04 GTest::gtest_main)
add_test(NAME c2_04 COMMAND test_c2_04)

# ==== 單元 c2_05 ====
# content/widgets/c2_05/CMakeLists.txt — C2-05 圖片 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_05 STATIC image_widget.cpp)
target_include_directories(c2_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_05 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元是組裝型 widget——
#   - c1_01（`ds::profiles::SkinProfile`）：widget 掛載的基底（渲染輸出綁定其具名 surface id）。
#   - e4_02（`ds::elements::ImageElement`）：單張圖片本體（configure/set_image/render_model
#     皆環繞其 set_source/set_scale_mode/render_model 的薄封裝；widget 層另維護多圖清單）。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_05 PUBLIC c1_01 e4_02)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_05 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_05.cpp)
target_link_libraries(test_c2_05 PRIVATE c2_05 GTest::gtest_main)
add_test(NAME c2_05 COMMAND test_c2_05)

# ==== 單元 c2_06 ====
# content/widgets/c2_06/CMakeLists.txt — C2-06 動畫 / 序列圖 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_06 STATIC animation_widget.cpp)
target_include_directories(c2_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_06 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元是組裝型 widget——
#   - c1_01（`ds::profiles::SkinProfile`）：widget 掛載的基底（渲染輸出綁定其具名 surface id）。
#   - e4_07（`ds::elements::FrameAnimationElement`）：幀序列動畫本體（configure/play/pause/
#     advance/current_frame/render_model 皆為其薄封裝）。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_06 PUBLIC c1_01 e4_07)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_06 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_06.cpp)
target_link_libraries(test_c2_06 PRIVATE c2_06 GTest::gtest_main)
add_test(NAME c2_06 COMMAND test_c2_06)

# ==== 單元 c2_07 ====
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

# ==== 單元 c2_08 ====
# content/widgets/c2_08/CMakeLists.txt — C2-08 筆記 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_08 STATIC note_widget.cpp)
target_include_directories(c2_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_08 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝四個擴充點——
#   - c1_01（C1-01 skin profile 基底：桌面存在——圖層 / 輸入策略 / 透明外形 / 定位 + 拖曳；
#     PUBLIC 相依 e1_01 e1_02 e1_03 e1_07 e1_08 e7_01）
#   - e4_01（E4-01 文字排版與渲染，經 e4_15 內部持有；PUBLIC 相依 e1_03）
#   - e4_15（E4-15 就地輸入框：筆記內容的編輯狀態機 + render_model；PUBLIC 相依 e4_01 e5_13）
#   - e7_12（E7-12 設定值寫回：筆記內容序列化 / 還原；PUBLIC 相依 e7_01）
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_08 PUBLIC c1_01 e4_01 e4_15 e7_12)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_08 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_08.cpp)
target_link_libraries(test_c2_08 PRIVATE c2_08 GTest::gtest_main)
add_test(NAME c2_08 COMMAND test_c2_08)

# ==== 單元 c2_09 ====
# content/widgets/c2_09/CMakeLists.txt — C2-09 行事曆 / 待辦 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_09 STATIC calendar_todo_widget.cpp)
target_include_directories(c2_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_09 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝四個擴充點——
#   - c1_01（`SkinProfile`）：桌面殼層基底（圖層 / 定位 / 拖曳），PUBLIC 相依 e1_01 e1_02 e1_03
#     e1_07 e1_08 e7_01。
#   - e4_01（`TextLayout`）：以文字呈現行事曆 / 待辦清單，PUBLIC 相依 e1_03。
#   - e7_12（`serialize`）：save() 序列化存載，PUBLIC 相依 e7_01。
#   - e7_13（`Item` / `build_forest`）：load() 建構日期分組 → 事項的樹狀結構，PUBLIC 相依 e7_01。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_09 PUBLIC c1_01 e4_01 e7_12 e7_13)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_09 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_09.cpp)
target_link_libraries(test_c2_09 PRIVATE c2_09 GTest::gtest_main)
add_test(NAME c2_09 COMMAND test_c2_09)

# ==== 單元 c2_10 ====
# content/widgets/c2_10/CMakeLists.txt — C2-10 網頁 widget
# 根 CMakeLists 以 content/*/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c2_10 STATIC web_widget.cpp)
target_include_directories(c2_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c2_10 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝兩個擴充點——
#   - c1_01（`SkinProfile`）：桌面殼層基底（圖層 / 定位 / 拖曳），PUBLIC 相依 e1_01 e1_02 e1_03
#     e1_07 e1_08 e7_01。
#   - e10_05（`WidgetHost`）：隔離行程 widget 宿主（本單元「網頁內容」的注入式後端 / 能力閘控
#     過的畫面 surface 橋接），PUBLIC 相依 e10_01 e1_03。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c2_10 PUBLIC c1_01 e10_05)

# 單元測試在 tests/c2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c2_10 ${CMAKE_SOURCE_DIR}/tests/c2/test_c2_10.cpp)
target_link_libraries(test_c2_10 PRIVATE c2_10 GTest::gtest_main)
add_test(NAME c2_10 COMMAND test_c2_10)
