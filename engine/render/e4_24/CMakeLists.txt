# engine/render/e4_24/CMakeLists.txt — E4-24 反鋸齒與內距
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e4_24 STATIC render_style.cpp)
target_include_directories(e4_24 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_24 PUBLIC cxx_std_17)

# 相依上游 E1-03（逐像素 alpha surface / surface 幾何模型，已合併可讀不可改）：
# 本單元的反鋸齒 / 內距渲染設定與 E1-03 同屬「渲染 surface 之上的宣告式設定層」，
# 承其具名鍵 / 正規化比例 / 明確狀態回報不靜默的設計精神（見 render_style.hpp 開頭註解）。
target_link_libraries(e4_24 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_24 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_24.cpp)
target_link_libraries(test_e4_24 PRIVATE e4_24 GTest::gtest_main)
add_test(NAME e4_24 COMMAND test_e4_24)
