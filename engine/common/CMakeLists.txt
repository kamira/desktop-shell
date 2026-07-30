# engine/common/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e12_01 ====
# engine/common/e12_01/CMakeLists.txt — E12-01 idle 記憶體與 CPU 門檻
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e12_01 STATIC idle_threshold.cpp)
target_include_directories(e12_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e12_01 PUBLIC cxx_std_17)

# 相依 E2-02 採集頻率分級與除頻（沿用 SamplingTier 作採集分級身分，閒置降級即在其上操作）。
# PUBLIC：本單元公開標頭 include 了 sampling.hpp，故傳遞相依給下游消費者。
target_link_libraries(e12_01 PUBLIC e2_02)

# 契約測試在 tests/e12/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e12_01 ${CMAKE_SOURCE_DIR}/tests/e12/test_e12_01.cpp)
target_link_libraries(test_e12_01 PRIVATE e12_01 GTest::gtest_main)
add_test(NAME e12_01 COMMAND test_e12_01)

# ==== 單元 e12_02 ====
# engine/common/e12_02/CMakeLists.txt — E12-02 多語系
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e12_02 STATIC locale_catalog.cpp)
target_include_directories(e12_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e12_02 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_01：本單元產出的在地化文字即 E4-01 TextLayout::layout() 的輸入字串
#     （翻譯在排版之前完成；本單元標頭不 include e4_01 標頭，但依 BUILD.md 慣例
#     宣告相依 target，供未來消費者透過本 target 一併取得排版依賴）。
target_link_libraries(e12_02 PUBLIC e4_01)

# 契約測試在 tests/e12/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e12_02 ${CMAKE_SOURCE_DIR}/tests/e12/test_e12_02.cpp)
target_link_libraries(test_e12_02 PRIVATE e12_02 GTest::gtest_main)
add_test(NAME e12_02 COMMAND test_e12_02)
