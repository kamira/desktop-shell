# modules/elements/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e4_02 ====
# modules/elements/e4_02/CMakeLists.txt — E4-02 圖片元件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_02 STATIC image_element.cpp)
target_include_directories(e4_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_02 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03（逐像素 alpha surface）：透明度沿用其 AlphaProfile；並透過其標頭傳遞
#     ds::kernel::SurfaceId（具名目標 surface）。
# PUBLIC：本單元標頭 include alpha_surface.hpp，需傳遞其 include 目錄。
target_link_libraries(e4_02 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_02 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_02.cpp)
target_link_libraries(test_e4_02 PRIVATE e4_02 GTest::gtest_main)
add_test(NAME e4_02 COMMAND test_e4_02)

# ==== 單元 e4_03 ====
# modules/elements/e4_03/CMakeLists.txt — E4-03 長條 / 進度 / 量表
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_03 STATIC bar_gauge.cpp)
target_include_directories(e4_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_03 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03：以具名 SurfaceId 指涉元件所在 surface（其標頭 alpha_surface.hpp 於本單元標頭 include）。
#   - e7_03：以宣告式「變數段落」文件驅動設定（section_vars.hpp 的 Value / Document / expand）。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_03 PUBLIC e1_03 e7_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_03 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_03.cpp)
target_link_libraries(test_e4_03 PRIVATE e4_03 GTest::gtest_main)
add_test(NAME e4_03 COMMAND test_e4_03)

# ==== 單元 e4_04 ====
# modules/elements/e4_04/CMakeLists.txt — E4-04 按鈕三態
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_04 STATIC button_element.cpp)
target_include_directories(e4_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_04 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_02（圖片元件）：各態視覺以 ImageElement 表達，render_model() 消費 ImageRenderModel。
#   - e1_04（幾何命中測試）：press/release 以 HitTester 對按鈕自身形狀做命中判定。
#   - e5_02（懸停進出事件）：以 HoverTracker 訂閱懸停事件切換 hover 態
#     （e5_02 已 PUBLIC 連 e1_04，故 e1_04 的 include 目錄透過 e5_02 亦傳遞可得；
#     此處仍顯式列出 e1_04 以彰顯直接依賴關係）。
# PUBLIC：本單元標頭 include 三者標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_04 PUBLIC e4_02 e1_04 e5_02)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_04 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_04.cpp)
target_link_libraries(test_e4_04 PRIVATE e4_04 GTest::gtest_main)
add_test(NAME e4_04 COMMAND test_e4_04)

# ==== 單元 e4_07 ====
# modules/elements/e4_07/CMakeLists.txt — E4-07 幀序列動畫
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_07 STATIC frame_animation_element.cpp)
target_include_directories(e4_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_07 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_02：圖片元件（ImageElement / ImageSource / MemoryImageSource / ImageRenderModel 等）。
#   - e4_09：動畫驅動源（AnimationDriver / AnimationId / Tick）。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_07 PUBLIC e4_02 e4_09)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_07 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_07.cpp)
target_link_libraries(test_e4_07 PRIVATE e4_07 GTest::gtest_main)
add_test(NAME e4_07 COMMAND test_e4_07)

# ==== 單元 e4_10 ====
# modules/elements/e4_10/CMakeLists.txt — E4-10 閒置自動動畫觸發
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_10 STATIC idle_animation_trigger.cpp)
target_include_directories(e4_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_10 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_07：幀序列動畫（FrameAnimationElement）。
#   - e5_09：機率排程器（ProbabilisticScheduler / RandomSource）。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_10 PUBLIC e4_07 e5_09)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_10 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_10.cpp)
target_link_libraries(test_e4_10 PRIVATE e4_10 GTest::gtest_main)
add_test(NAME e4_10 COMMAND test_e4_10)

# ==== 單元 e4_11 ====
# modules/elements/e4_11/CMakeLists.txt — E4-11 逐字顯示
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_11 STATIC typewriter_element.cpp)
target_include_directories(e4_11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_11 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01：文字排版（TextLayout / LayoutConstraints / LayoutResult）。
#   - e4_09：動畫驅動源（AnimationDriver / AnimationId / Tick）。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_11 PUBLIC e4_01 e4_09)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_11 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_11.cpp)
target_link_libraries(test_e4_11 PRIVATE e4_11 GTest::gtest_main)
add_test(NAME e4_11 COMMAND test_e4_11)

# ==== 單元 e4_12 ====
# modules/elements/e4_12/CMakeLists.txt — E4-12 可點文字區段
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_12 STATIC clickable_text.cpp)
target_include_directories(e4_12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_12 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01：文字排版引擎（TextLayout / LayoutResult / decode_utf8），取字符相對幾何。
#   - e1_04：幾何命中測試（HitTester / LocalPoint / Shape），判點擊落在哪個字符/區段。
# PUBLIC：本單元標頭 include 兩者標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_12 PUBLIC e4_01 e1_04)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_12 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_12.cpp)
target_link_libraries(test_e4_12 PRIVATE e4_12 GTest::gtest_main)
add_test(NAME e4_12 COMMAND test_e4_12)

# ==== 單元 e4_13 ====
# modules/elements/e4_13/CMakeLists.txt — E4-13 文字流內嵌物件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_13 STATIC inline_flow_element.cpp)
target_include_directories(e4_13 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_13 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01（文字排版）：重用其 FontMetrics / LayoutConstraints / decode_utf8 做換行/度量。
#   - e4_02（圖片元件）：重用其 ImageRenderModel 做內嵌物件的顯示描述。
# PUBLIC：本單元標頭 include 兩者標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_13 PUBLIC e4_01 e4_02)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_13 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_13.cpp)
target_link_libraries(test_e4_13 PRIVATE e4_13 GTest::gtest_main)
add_test(NAME e4_13 COMMAND test_e4_13)

# ==== 單元 e4_14 ====
# modules/elements/e4_14/CMakeLists.txt — E4-14 長文捲動與分頁
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_14 STATIC scroll_view.cpp)
target_include_directories(e4_14 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_14 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01：文字排版結果 LayoutResult（`set_content` 取用其 `lines.size()` 作為捲動範圍）。
# PUBLIC：本單元標頭 include 上游標頭 text_layout.hpp，需傳遞其 include 目錄給下游。
target_link_libraries(e4_14 PUBLIC e4_01)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_14 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_14.cpp)
target_link_libraries(test_e4_14 PRIVATE e4_14 GTest::gtest_main)
add_test(NAME e4_14 COMMAND test_e4_14)

# ==== 單元 e4_15 ====
# modules/elements/e4_15/CMakeLists.txt — E4-15 就地輸入框
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_15 STATIC text_input_element.cpp)
target_include_directories(e4_15 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_15 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01：以 TextLayout 排版顯示文字（render_model() 消費 LayoutResult）。
#   - e5_13：以 KeyboardInputSource 接收注入式輸入事件（attach() 訂閱按鍵 / 文字提交頻道）。
# PUBLIC：本單元標頭 include 兩者標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_15 PUBLIC e4_01 e5_13)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_15 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_15.cpp)
target_link_libraries(test_e4_15 PRIVATE e4_15 GTest::gtest_main)
add_test(NAME e4_15 COMMAND test_e4_15)

# ==== 單元 e4_16 ====
# modules/elements/e4_16/CMakeLists.txt — E4-16 輸入建議與補全呈現
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_16 STATIC suggestion_popup_element.cpp)
target_include_directories(e4_16 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_16 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01：以 TextLayout 排版每一候選項文字（render_model() 消費 LayoutResult）。
#   - e4_15：綁定 TextInputElement（accept() 套用補全的目標）；其 PUBLIC 連結亦轉遞
#     e5_13（KeyboardInputSource / KeyEvent），供本單元 handle_key()/attach() 使用，
#     無需本單元再另外連結。
# PUBLIC：本單元標頭 include 兩者標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_16 PUBLIC e4_01 e4_15)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_16 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_16.cpp)
target_link_libraries(test_e4_16 PRIVATE e4_16 GTest::gtest_main)
add_test(NAME e4_16 COMMAND test_e4_16)

# ==== 單元 e4_17 ====
# modules/elements/e4_17/CMakeLists.txt — E4-17 直方圖
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_17 STATIC histogram.cpp)
target_include_directories(e4_17 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_17 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03（逐像素 alpha surface）：合成描述沿用其 AlphaProfile / AlphaMode（半透明 widget 合成）。
#   - e7_03（段落變數）：宣告式設定驅動（configure_from_document 先以其 expand() 展開變數）。
# PUBLIC：本單元標頭 include alpha_surface.hpp 與 section_vars.hpp，需傳遞其 include 目錄。
target_link_libraries(e4_17 PUBLIC e1_03 e7_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_17 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_17.cpp)
target_link_libraries(test_e4_17 PRIVATE e4_17 GTest::gtest_main)
add_test(NAME e4_17 COMMAND test_e4_17)

# ==== 單元 e4_18 ====
# modules/elements/e4_18/CMakeLists.txt — E4-18 徑向線 / 指針
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_18 STATIC radial_pointer.cpp)
target_include_directories(e4_18 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_18 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03：以具名 SurfaceId 指涉元件所在 surface（其標頭 alpha_surface.hpp 於本單元標頭 include）。
#   - e7_03：以宣告式「變數段落」文件驅動設定（section_vars.hpp 的 Value / Document / expand）。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_18 PUBLIC e1_03 e7_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_18 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_18.cpp)
target_link_libraries(test_e4_18 PRIVATE e4_18 GTest::gtest_main)
add_test(NAME e4_18 COMMAND test_e4_18)

# ==== 單元 e4_19 ====
# modules/elements/e4_19/CMakeLists.txt — E4-19 旋轉元件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_19 STATIC rotatable_element.cpp)
target_include_directories(e4_19 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_19 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_02：圖片元件（ImageElement / ImageSource / MemoryImageSource / ImageRenderModel 等）。
#   - e4_22：2D 變形矩陣（Transform2D / Vec2），用以計算繞具名 pivot 的旋轉矩陣。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_19 PUBLIC e4_02 e4_22)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_19 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_19.cpp)
target_link_libraries(test_e4_19 PRIVATE e4_19 GTest::gtest_main)
add_test(NAME e4_19 COMMAND test_e4_19)

# ==== 單元 e4_20 ====
# modules/elements/e4_20/CMakeLists.txt — E4-20 向量圖形
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_20 STATIC vector_graphic.cpp)
target_include_directories(e4_20 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_20 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03（逐像素 alpha surface）：描邊 / 填色的合成描述沿用其 AlphaProfile / AlphaMode。
# PUBLIC：本單元標頭 include alpha_surface.hpp，需傳遞其 include 目錄。
target_link_libraries(e4_20 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_20 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_20.cpp)
target_link_libraries(test_e4_20 PRIVATE e4_20 GTest::gtest_main)
add_test(NAME e4_20 COMMAND test_e4_20)

# ==== 單元 e4_21 ====
# modules/elements/e4_21/CMakeLists.txt — E4-21 漸層填色
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_21 STATIC gradient_fill.cpp)
target_include_directories(e4_21 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_21 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03（逐像素 alpha surface）：漸層填色的合成描述沿用其 AlphaProfile / AlphaMode
#     （漸層最終如何與所在 surface 合成；opacity 為比例，符合 NFR-02）。
# PUBLIC：本單元標頭 include alpha_surface.hpp，需傳遞其 include 目錄給下游。
target_link_libraries(e4_21 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_21 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_21.cpp)
target_link_libraries(test_e4_21 PRIVATE e4_21 GTest::gtest_main)
add_test(NAME e4_21 COMMAND test_e4_21)

# ==== 單元 e4_25 ====
# modules/elements/e4_25/CMakeLists.txt — E4-25 背景模式
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_25 STATIC background_element.cpp)
target_include_directories(e4_25 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_25 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03（逐像素 alpha surface）：透明度沿用其 AlphaProfile；並透過其標頭傳遞
#     ds::kernel::SurfaceId（具名目標 surface）。
#   - e4_02（圖片元件）：圖片背景直接重用其 ImageElement / ImageSource / ImageRenderModel，
#     本單元不重造圖片解碼 / 縮放 / 裁切邏輯。
# PUBLIC：本單元標頭 include 兩者標頭，需傳遞其 include 目錄。
target_link_libraries(e4_25 PUBLIC e1_03 e4_02)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_25 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_25.cpp)
target_link_libraries(test_e4_25 PRIVATE e4_25 GTest::gtest_main)
add_test(NAME e4_25 COMMAND test_e4_25)

# ==== 單元 e4_26 ====
# modules/elements/e4_26/CMakeLists.txt — E4-26 音效播放
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_26 STATIC audio_player.cpp)
target_include_directories(e4_26 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_26 PUBLIC cxx_std_17)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_26 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_26.cpp)
target_link_libraries(test_e4_26 PRIVATE e4_26 GTest::gtest_main)
add_test(NAME e4_26 COMMAND test_e4_26)

# ==== 單元 e4_27 ====
# modules/elements/e4_27/CMakeLists.txt — E4-27 綁定式合成（著替）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_27 STATIC bound_composite.cpp)
target_include_directories(e4_27 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_27 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_08（圖層合成）：本單元 compose() 內部暫用其 LayerCompositor 完成實際合成計畫
#     產出；標頭 include layer_compositor.hpp（其鏈上又 include surface_switcher.hpp /
#     image_element.hpp），需 PUBLIC 傳遞其 include 目錄與型別。
target_link_libraries(e4_27 PUBLIC e4_08)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_27 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_27.cpp)
target_link_libraries(test_e4_27 PRIVATE e4_27 GTest::gtest_main)
add_test(NAME e4_27 COMMAND test_e4_27)

# ==== 單元 e4_28 ====
# modules/elements/e4_28/CMakeLists.txt — E4-28 透視與景深呈現
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_28 STATIC perspective_element.cpp)
target_include_directories(e4_28 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_28 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_22：2D 變形矩陣（Transform2D / Vec2），用以計算依相對深度換算的透視變形（縮放 +
#     朝具名消失點方向的視差位移）。
# PUBLIC：本單元標頭 include transform2d.hpp，需傳遞其 include 目錄給下游。
target_link_libraries(e4_28 PUBLIC e4_22)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_28 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_28.cpp)
target_link_libraries(test_e4_28 PRIVATE e4_28 GTest::gtest_main)
add_test(NAME e4_28 COMMAND test_e4_28)

# ==== 單元 e4_29 ====
# modules/elements/e4_29/CMakeLists.txt — E4-29 折線圖
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_29 STATIC line_chart.cpp)
target_include_directories(e4_29 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_29 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_17（直方圖）：沿用其 HistogramRange / HistogramStatus 型別（y 軸值域 / 操作結果碼），
#     並經其 PUBLIC 連結傳遞 e1_03（AlphaProfile 合成描述）的 include 目錄。
# PUBLIC：本單元標頭 include histogram.hpp（進而 alpha_surface.hpp），需傳遞其 include 目錄。
target_link_libraries(e4_29 PUBLIC e4_17)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_29 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_29.cpp)
target_link_libraries(test_e4_29 PRIVATE e4_29 GTest::gtest_main)
add_test(NAME e4_29 COMMAND test_e4_29)

# ==== 單元 e4_30 ====
# modules/elements/e4_30/CMakeLists.txt — E4-30 全螢幕調光覆蓋
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_30 STATIC dim_overlay.cpp)
target_include_directories(e4_30 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_30 PUBLIC cxx_std_17)

# 上游相依（可讀不可改）：
#   - e1_03：per-pixel alpha surface（AlphaSurfaceService），覆蓋層以此表達半透明。
#   - e1_01：具名圖層堆疊（LayerStack / layer_name），把覆蓋層置於 Topmost（NFR-02）。
# PUBLIC：本單元標頭 include 兩者標頭（及其傳遞的 null_backend.hpp），故傳遞其 include 目錄。
target_link_libraries(e4_30 PUBLIC e1_03 e1_01)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_30 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_30.cpp)
target_link_libraries(test_e4_30 PRIVATE e4_30 GTest::gtest_main)
add_test(NAME e4_30 COMMAND test_e4_30)
