# modules/elements/e4_30/CMakeLists.txt — E4-30 全螢幕調光覆蓋
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_30 STATIC dim_overlay.cpp)
target_include_directories(e4_30 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_30 PUBLIC cxx_std_17)

# 上游相依（可讀不可改）：
#   - e1_03：per-pixel alpha surface（AlphaSurfaceService），覆蓋層以此表達半透明。
#   - e1_01：具名圖層堆疊（LayerStack / layer_name），把覆蓋層置於 Topmost（NFR-02）。
# PUBLIC：本單元標頭 include 兩者標頭（及其傳遞的 null_backend.hpp），故傳遞其 include 目錄。
target_link_libraries(e4_30 PUBLIC e1_03 e1_01)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_30 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_30.cpp)
target_link_libraries(test_e4_30 PRIVATE e4_30 GTest::gtest_main)
add_test(NAME e4_30 COMMAND test_e4_30)
