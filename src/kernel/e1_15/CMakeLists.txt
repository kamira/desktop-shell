# src/kernel/e1_15/CMakeLists.txt — E1-15 視窗級透明度與懸停淡變
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_15 STATIC window_opacity.cpp)
target_include_directories(e1_15 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_15 PUBLIC cxx_std_17)

# 相依上游（可讀不可改）：
#   - e1_03（AlphaSurfaceService / AlphaProfile / AlphaStatus）：本單元透過其 set_opacity 把
#     計算出的目前不透明度實際套用至具名 surface；能力閘控（NFR-03）/ opacity 正規化皆由其負責。
#   - e5_02（HoverTracker / HoverEvent）：本單元訂閱其懸停事件驅動淡變起停。
target_link_libraries(e1_15 PUBLIC e1_03 e5_02)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_15 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_15.cpp)
target_link_libraries(test_e1_15 PRIVATE e1_15 GTest::gtest_main)
add_test(NAME e1_15 COMMAND test_e1_15)
