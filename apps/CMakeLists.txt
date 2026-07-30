# apps/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 c4_01 ====
# apps/c4_01/CMakeLists.txt — C4-01 鍵位速查
# 根 CMakeLists 以 apps/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c4_01 STATIC shortcut_cheat_sheet.cpp)
target_include_directories(c4_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c4_01 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝兩個擴充點——
#   - c1_04：OSD 浮層 profile（OsdOverlayProfile），本單元掛為速查面板的顯隱基底
#     （show/update/dismiss，PUBLIC 相依 e1_01 + e1_02 + e1_14）。
#   - e4_01：文字排版與渲染（TextLayout / FontMetrics / LayoutResult），供把可見鍵位清單
#     排版成渲染描述（PUBLIC 相依 e1_03）。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c4_01 PUBLIC c1_04 e4_01)

# 單元測試在 tests/c4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c4_01 ${CMAKE_SOURCE_DIR}/tests/c4/test_c4_01.cpp)
target_link_libraries(test_c4_01 PRIVATE c4_01 GTest::gtest_main)
add_test(NAME c4_01 COMMAND test_c4_01)

# ==== 單元 c4_02 ====
# apps/c4_02/CMakeLists.txt — C4-02 截圖與標註
# 根 CMakeLists 以 apps/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c4_02 STATIC screenshot_annotator.cpp)
target_include_directories(c4_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c4_02 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝三個擴充點——
#   - e3_11：螢幕擷取致動器（ScreenCaptureBackend / CaptureSpec / CaptureRegion / CaptureResult），
#     供 capture() 以注入式後端擷取指定區域。
#   - e4_30：全螢幕調光覆蓋（DimOverlayElement），借它組出選取框覆蓋。
#   - e4_20：向量圖形（VectorGraphic / Shape / build_shape_path），供 annotate_arrow /
#     annotate_box 把標註轉為向量渲染描述。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c4_02 PUBLIC e3_11 e4_30 e4_20)

# 單元測試在 tests/c4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c4_02 ${CMAKE_SOURCE_DIR}/tests/c4/test_c4_02.cpp)
target_link_libraries(test_c4_02 PRIVATE c4_02 GTest::gtest_main)
add_test(NAME c4_02 COMMAND test_c4_02)

# ==== 單元 c4_03 ====
# apps/c4_03/CMakeLists.txt — C4-03 取色器
# 根 CMakeLists 以 apps/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c4_03 STATIC color_picker.cpp)
target_include_directories(c4_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c4_03 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝兩個擴充點——
#   - e2_27：螢幕像素取樣（PixelSampleSource / PixelColor），供 pick_at() 取色。
#   - e4_30：全螢幕調光覆蓋（DimOverlayElement），借它組出放大鏡 / 取色游標。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c4_03 PUBLIC e2_27 e4_30)

# 單元測試在 tests/c4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c4_03 ${CMAKE_SOURCE_DIR}/tests/c4/test_c4_03.cpp)
target_link_libraries(test_c4_03 PRIVATE c4_03 GTest::gtest_main)
add_test(NAME c4_03 COMMAND test_c4_03)

# ==== 單元 c4_04 ====
# apps/c4_04/CMakeLists.txt — C4-04 視窗總覽
# 根 CMakeLists 以 apps/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c4_04 STATIC window_overview.cpp)
target_include_directories(c4_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c4_04 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝兩個擴充點——
#   - c1_05：召喚面板 profile（總覽本身的基底 profile，借用其開啟/收起短暫生命週期）。
#   - e4_02：圖片元件（每個視窗一個縮圖渲染描述，佔位圖像參照）。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c4_04 PUBLIC c1_05 e4_02)

# 單元測試在 tests/c4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c4_04 ${CMAKE_SOURCE_DIR}/tests/c4/test_c4_04.cpp)
target_link_libraries(test_c4_04 PRIVATE c4_04 GTest::gtest_main)
add_test(NAME c4_04 COMMAND test_c4_04)

# ==== 單元 c4_05 ====
# apps/c4_05/CMakeLists.txt — C4-05 AI 對話面板
# 根 CMakeLists 以 apps/*/CMakeLists.txt 的 GLOB 自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(c4_05 STATIC chat_panel.cpp)
target_include_directories(c4_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(c4_05 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：本單元組裝四個擴充點——
#   - c1_05：召喚面板 profile（SummonPanelProfile），供本面板的召喚 / 收起生命週期。
#   - e2_14：HTTP 取得與結構化解析（HttpFetchProvider），供上下文指標。
#   - e4_01：文字排版與渲染（TextLayout / FontMetrics），供草稿量測與訊息渲染描述。
#   - e4_11：逐字顯示（TypewriterElement），對話串每則訊息的顯示元素（內部消費 e4_01）。
# 標頭皆直接 include 其上游標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(c4_05 PUBLIC c1_05 e2_14 e4_01 e4_11)

# 單元測試在 tests/c4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_c4_05 ${CMAKE_SOURCE_DIR}/tests/c4/test_c4_05.cpp)
target_link_libraries(test_c4_05 PRIVATE c4_05 GTest::gtest_main)
add_test(NAME c4_05 COMMAND test_c4_05)
