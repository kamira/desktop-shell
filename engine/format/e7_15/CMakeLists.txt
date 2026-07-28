# engine/format/e7_15/CMakeLists.txt — E7-15 拖放產生設定項
# E7-12 設定值寫回 + E5-08 系統事件的應用（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e7_15 STATIC dropgen.cpp)
target_include_directories(e7_15 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_15 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   e7_12 → 設定值寫回（set_value / serialize；並傳遞 e7_01 的 Value / Document）。
#   e5_08 → 系統事件（SystemEvent / SystemEventSource；拖放內容經其事件分派注入）。
target_link_libraries(e7_15 PUBLIC e7_12 e5_08)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_15 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_15.cpp)
target_link_libraries(test_e7_15 PRIVATE e7_15 GTest::gtest_main)
add_test(NAME e7_15 COMMAND test_e7_15)
