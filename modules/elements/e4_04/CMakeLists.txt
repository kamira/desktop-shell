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
