# src/kernel/e1_09/CMakeLists.txt — E1-09 邊緣吸附
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_09 STATIC edge_snapping.cpp)
target_include_directories(e1_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_09 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_07（anchor 定位模型）：吸附後位置以宣告式 AnchorSpec（九宮錨點 + 正規化偏移）表達、
#     以 resolve() 落地（NFR-02 正解）。
#   - e1_08（自由拖曳與位置記憶）：snap_surface 便利層讀取拖曳中 surface 的實時位置（live_position）
#     並對其他具名 surface 的實時位置吸附（與拖曳整合）。
# PUBLIC：本單元標頭 include edge_snapping.hpp → anchor_model.hpp / draggable_surface.hpp，
# 需傳遞其 include 目錄。
target_link_libraries(e1_09 PUBLIC e1_07 e1_08)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_09 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_09.cpp)
target_link_libraries(test_e1_09 PRIVATE e1_09 GTest::gtest_main)
add_test(NAME e1_09 COMMAND test_e1_09)
