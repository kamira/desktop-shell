# src/kernel/e1_19/CMakeLists.txt — E1-19 顯示器熱插拔復原
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_19 STATIC layout_recovery.cpp)
target_include_directories(e1_19 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_19 PUBLIC cxx_std_17)

# 上游相依（已合併，可讀不可改）：
#   e1_18 顯示器拓撲抽象（ScreenRegistry / PerScreen / ScreenId）—— 標頭 include named_screens.hpp
#   e5_08 系統事件（SystemEventSource / DisplayChanged）—— 標頭 include system_event.hpp
# 皆 PUBLIC：本單元標頭對外暴露這些型別，須把上游 include 目錄向下游傳遞。
target_link_libraries(e1_19 PUBLIC e1_18 e5_08)

# 契約測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_19 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_19.cpp)
target_link_libraries(test_e1_19 PRIVATE e1_19 GTest::gtest_main)
add_test(NAME e1_19 COMMAND test_e1_19)
