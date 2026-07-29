# src/kernel/e1_13/CMakeLists.txt — E1-13 多 profile 實例並存
# 根 CMakeLists 以明確層級 GLOB（src/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_13 STATIC profile_instance_registry.cpp)
target_include_directories(e1_13 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_13 PUBLIC cxx_std_17)

# 上游相依（已合併，可讀不可改）：
#   - e1_01（具名圖層與 z-order LayerStack）：本單元標頭 include `layer_stack.hpp`
#     （組合一個 LayerStack 做具名圖層指派 + 能力矩陣單一資料來源），故 PUBLIC 傳遞其
#     include 目錄；e1_01 自身再 PUBLIC 傳遞 e1_21 / backend_null，故本單元亦間接可見
#     CapabilityMatrix / SurfaceProfile 等上游型別。
target_link_libraries(e1_13 PUBLIC e1_01)

# 單元測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_13 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_13.cpp)
target_link_libraries(test_e1_13 PRIVATE e1_13 GTest::gtest_main)
add_test(NAME e1_13 COMMAND test_e1_13)
