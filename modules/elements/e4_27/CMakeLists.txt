# modules/elements/e4_27/CMakeLists.txt — E4-27 綁定式合成（著替）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_27 STATIC bound_composite.cpp)
target_include_directories(e4_27 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_27 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_08（圖層合成）：本單元 compose() 內部暫用其 LayerCompositor 完成實際合成計畫
#     產出；標頭 include layer_compositor.hpp（其鏈上又 include surface_switcher.hpp /
#     image_element.hpp），需 PUBLIC 傳遞其 include 目錄與型別。
target_link_libraries(e4_27 PUBLIC e4_08)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_27 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_27.cpp)
target_link_libraries(test_e4_27 PRIVATE e4_27 GTest::gtest_main)
add_test(NAME e4_27 COMMAND test_e4_27)
