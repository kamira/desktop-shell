# modules/elements/e4_21/CMakeLists.txt — E4-21 漸層填色
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_21 STATIC gradient_fill.cpp)
target_include_directories(e4_21 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_21 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03（逐像素 alpha surface）：漸層填色的合成描述沿用其 AlphaProfile / AlphaMode
#     （漸層最終如何與所在 surface 合成；opacity 為比例，符合 NFR-02）。
# PUBLIC：本單元標頭 include alpha_surface.hpp，需傳遞其 include 目錄給下游。
target_link_libraries(e4_21 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_21 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_21.cpp)
target_link_libraries(test_e4_21 PRIVATE e4_21 GTest::gtest_main)
add_test(NAME e4_21 COMMAND test_e4_21)
