# engine/command/e6_05/CMakeLists.txt — E6-05 跨 surface 控制
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e6_05 STATIC cross_surface_controller.cpp)
target_include_directories(e6_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e6_05 PUBLIC cxx_std_17)

# 上游相依（已合併，可讀不可改）：
#   - e6_01（命令匯流排 CommandBus / CommandArgs / CommandResult）：本單元標頭 include
#     `command_bus.hpp`，經其實際分派命令。
#   - e1_13（多 profile 實例並存 ProfileInstanceRegistry）：本單元標頭 include
#     `profile_instance_registry.hpp`，經其列舉 / 驗證具名目標（InstanceId）。
target_link_libraries(e6_05 PUBLIC e6_01 e1_13)

# 單元測試在 tests/e6/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e6_05 ${CMAKE_SOURCE_DIR}/tests/e6/test_e6_05.cpp)
target_link_libraries(test_e6_05 PRIVATE e6_05 GTest::gtest_main)
add_test(NAME e6_05 COMMAND test_e6_05)
