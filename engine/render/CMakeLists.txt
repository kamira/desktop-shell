# engine/render/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e4_01 ====
# engine/render/e4_01/CMakeLists.txt — E4-01 文字排版與渲染
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，engine/*/*/CMakeLists.txt）。
add_library(e4_01 STATIC text_layout.cpp)
target_include_directories(e4_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_01 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e1_03：以具名 SurfaceId 指涉排版目標 surface（其標頭 alpha_surface.hpp 於本單元標頭 include）。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_01 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_01 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_01.cpp)
target_link_libraries(test_e4_01 PRIVATE e4_01 GTest::gtest_main)
add_test(NAME e4_01 COMMAND test_e4_01)

# ==== 單元 e4_05 ====
# engine/render/e4_05/CMakeLists.txt — E4-05 過場動畫與緩動
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e4_05 STATIC easing.cpp tween.cpp)
target_include_directories(e4_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_05 PUBLIC cxx_std_17)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_05 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_05.cpp)
target_link_libraries(test_e4_05 PRIVATE e4_05 GTest::gtest_main)
add_test(NAME e4_05 COMMAND test_e4_05)

# ==== 單元 e4_06 ====
# engine/render/e4_06/CMakeLists.txt — E4-06 Surface 編號定址與切換
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e4_06 STATIC surface_switcher.cpp)
target_include_directories(e4_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_06 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_02（圖片元件）：本單元僅借道其標頭 image_element.hpp 傳遞 ds::kernel::SurfaceId
#     （經其 include 的 e1_03/alpha_surface.hpp -> backend_null/null_backend.hpp 一路 PUBLIC
#     傳遞的具名 surface 識別碼型別），沿用既有型別、不重造一套 id。
#     PUBLIC：本單元標頭 include image_element.hpp，需傳遞其 include 目錄。
target_link_libraries(e4_06 PUBLIC e4_02)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_06 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_06.cpp)
target_link_libraries(test_e4_06 PRIVATE e4_06 GTest::gtest_main)
add_test(NAME e4_06 COMMAND test_e4_06)

# ==== 單元 e4_08 ====
# engine/render/e4_08/CMakeLists.txt — E4-08 圖層合成
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e4_08 STATIC layer_compositor.cpp)
target_include_directories(e4_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_08 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_02（圖片元件）：依規格連結；本單元標頭經 e4_06 -> e4_02 的 include 鏈取得
#     ds::kernel::SurfaceId，不使用 ImageElement 本身。
#   - e4_06（Surface 編號定址與切換）：本單元圖層以其 SurfaceSwitcher::has() 做定址存在性
#     檢查，確保合成計畫只描述已知的具名 surface。
# PUBLIC：本單元標頭 include surface_switcher.hpp（其鏈上又 include image_element.hpp），
# 需傳遞兩者的 include 目錄。
target_link_libraries(e4_08 PUBLIC e4_02 e4_06)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_08 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_08.cpp)
target_link_libraries(test_e4_08 PRIVATE e4_08 GTest::gtest_main)
add_test(NAME e4_08 COMMAND test_e4_08)

# ==== 單元 e4_09 ====
# engine/render/e4_09/CMakeLists.txt — E4-09 動畫驅動源
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e4_09 STATIC animation_driver.cpp)
target_include_directories(e4_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_09 PUBLIC cxx_std_17)

# 相依上游 E5-04 心跳事件（已合併，可讀不可改）：PUBLIC 傳遞其標頭與介面。
target_link_libraries(e4_09 PUBLIC e5_04)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_09 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_09.cpp)
target_link_libraries(test_e4_09 PRIVATE e4_09 GTest::gtest_main)
add_test(NAME e4_09 COMMAND test_e4_09)

# ==== 單元 e4_22 ====
# engine/render/e4_22/CMakeLists.txt — E4-22 2D 變形矩陣
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e4_22 STATIC transform2d.cpp)
target_include_directories(e4_22 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_22 PUBLIC cxx_std_17)

# 相依上游 E1-03（逐像素 alpha surface / surface 幾何模型，已合併可讀不可改）：
# 本 2D 變形矩陣為供渲染層變形 surface 幾何的數學工具，宣告於 surface 模型之上。
target_link_libraries(e4_22 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_22 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_22.cpp)
target_link_libraries(test_e4_22 PRIVATE e4_22 GTest::gtest_main)
add_test(NAME e4_22 COMMAND test_e4_22)

# ==== 單元 e4_23 ====
# engine/render/e4_23/CMakeLists.txt — E4-23 容器裁切與遮罩
# 根 CMakeLists 以層級 GLOB（engine/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e4_23 STATIC clip_mask.cpp)
target_include_directories(e4_23 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_23 PUBLIC cxx_std_17)

# 相依上游 E1-03（逐像素 alpha surface / surface 具名指涉慣例，已合併可讀不可改）：
# 本裁切與遮罩服務沿用其 ds::kernel::SurfaceId 具名容器指涉慣例（PUBLIC：本單元標頭
# include null_backend.hpp，需傳遞 include 目錄）。
target_link_libraries(e4_23 PUBLIC e1_03)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_23 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_23.cpp)
target_link_libraries(test_e4_23 PRIVATE e4_23 GTest::gtest_main)
add_test(NAME e4_23 COMMAND test_e4_23)

# ==== 單元 e4_24 ====
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
