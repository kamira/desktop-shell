# engine/script/e8_03/CMakeLists.txt — E8-03 表現控制指令集
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e8_03 STATIC presentation_commands.cpp)
target_include_directories(e8_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e8_03 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e8_02：與 E8-02 對話腳本直譯器整合的介面（OutputSink），本單元的 PresentationSink
#     實作它，讓既有 say 步驟可編排表現指令。
#   - e4_06：switch_surface 指令委派給 SurfaceSwitcher 做實際切換。
#   - e6_01：show/hide/switch_surface/transition/wait 一律經 CommandBus 具名分派。
# PUBLIC：本單元標頭 include 三者的標頭，需傳遞其 include 目錄 / 型別。
target_link_libraries(e8_03 PUBLIC e8_02 e4_06 e6_01)

# 測試檔在 tests/e8/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e8_03 ${CMAKE_SOURCE_DIR}/tests/e8/test_e8_03.cpp)
target_link_libraries(test_e8_03 PRIVATE e8_03 GTest::gtest_main)
add_test(NAME e8_03 COMMAND test_e8_03)
