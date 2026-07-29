# src/kernel/e1_08/CMakeLists.txt — E1-08 自由拖曳與位置記憶
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_08 STATIC draggable_surface.cpp)
target_include_directories(e1_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_08 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03（逐像素 alpha surface）：傳遞取得 E1-24 KernelBackend / SurfaceId surface 模型；
#     拖曳操作以 backend.has_surface(id) 閘控「只拖曳真實存在的 surface」。
#   - e1_07（anchor 定位模型）：位置以宣告式 AnchorSpec（九宮錨點 + 正規化偏移）表達、
#     以 resolve() 落地（NFR-02 正解）。
#   - e7_12（設定值寫回，其上為 E7-01 格式核心）：位置記憶的序列化 / 反序列化（設定持久化）。
# PUBLIC：本單元標頭 include anchor_model.hpp / null_backend.hpp，需傳遞其 include 目錄。
target_link_libraries(e1_08 PUBLIC e1_03 e1_07 e7_12)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_08 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_08.cpp)
target_link_libraries(test_e1_08 PRIVATE e1_08 GTest::gtest_main)
add_test(NAME e1_08 COMMAND test_e1_08)
