# src/kernel/e1_20/CMakeLists.txt — E1-20 睡眠喚醒復原
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_20 STATIC sleep_wake_recovery.cpp)
target_include_directories(e1_20 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_20 PUBLIC cxx_std_17)

# 上游相依（已合併，可讀不可改）：
#   e1_01 具名圖層堆疊（LayerStack / SurfaceId / SurfaceLayer）—— 標頭 include layer_stack.hpp
#   e5_08 系統事件（SystemEventSource / SystemSleep / SystemWake）—— 標頭 include system_event.hpp
# 皆 PUBLIC：本單元標頭對外暴露這些型別，須把上游 include 目錄向下游傳遞。
target_link_libraries(e1_20 PUBLIC e1_01 e5_08)

# 契約測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_20 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_20.cpp)
target_link_libraries(test_e1_20 PRIVATE e1_20 GTest::gtest_main)
add_test(NAME e1_20 COMMAND test_e1_20)
