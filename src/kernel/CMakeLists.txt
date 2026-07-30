# src/kernel/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e1_01 ====
# src/kernel/e1_01/CMakeLists.txt — E1-01 具名圖層列舉與 z-order 維持
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_01 STATIC layer_stack.cpp)
target_include_directories(e1_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_01 PUBLIC cxx_std_17)

# 上游相依（可讀不可改）：
#   - e1_21：CapabilityMatrix / has() 能力閘控（NFR-03）。
#   - backend_null：具名圖層詞彙 SurfaceLayer / SurfaceId（NFR-02，本單元建於其上）。
# PUBLIC：本單元標頭 include 兩者標頭，故傳遞其 include 目錄。
target_link_libraries(e1_01 PUBLIC e1_21 backend_null)

# 契約 / 單元測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_01 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_01.cpp)
target_link_libraries(test_e1_01 PRIVATE e1_01 GTest::gtest_main)
add_test(NAME e1_01 COMMAND test_e1_01)

# ==== 單元 e1_02 ====
# src/kernel/e1_02/CMakeLists.txt — E1-02 輸入策略四態
# 根 CMakeLists 以明確層級 GLOB（src/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_02 STATIC input_strategy.cpp)
target_include_directories(e1_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_02 PUBLIC cxx_std_17)

# 上游相依（可讀不可改）：
#   - backend_null（E1-24）：本單元標頭 include `null_backend.hpp`（KernelBackend / InputPolicy
#     / InputEvent / SurfaceId），故 PUBLIC 傳遞其 include 目錄。
#   - e1_21（能力矩陣）：CapabilityId / has()（NFR-03 閘控）。backend_null 已 PUBLIC 依賴之，
#     此處依單元規格顯式再列，語意清晰。
target_link_libraries(e1_02 PUBLIC e1_21 backend_null)

# 單元測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_02 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_02.cpp)
target_link_libraries(test_e1_02 PRIVATE e1_02 GTest::gtest_main)
add_test(NAME e1_02 COMMAND test_e1_02)

# ==== 單元 e1_03 ====
# src/kernel/e1_03/CMakeLists.txt — E1-03 逐像素 alpha surface
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_03 STATIC alpha_surface.cpp)
target_include_directories(e1_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_03 PUBLIC cxx_std_17)

# 相依上游：
#   - e1_21（能力矩陣）：per-pixel alpha 能力宣告 / has() 閘控的資料來源（NFR-03）。
#   - backend_null（E1-24）：提供 kernel 後端抽象介面 KernelBackend + SurfaceId / SurfaceProfile
#     surface 模型；本服務建於其上。PUBLIC：本單元標頭 include null_backend.hpp，需傳遞 include 目錄。
#     （backend_null 已 PUBLIC 連 e1_21，故 e1_21 的 include 目錄亦傳遞可得。）
target_link_libraries(e1_03 PUBLIC e1_21 backend_null)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_03 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_03.cpp)
target_link_libraries(test_e1_03 PRIVATE e1_03 GTest::gtest_main)
add_test(NAME e1_03 COMMAND test_e1_03)

# ==== 單元 e1_04 ====
# src/kernel/e1_04/CMakeLists.txt — E1-04 幾何命中測試
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_04 STATIC hit_test.cpp)
target_include_directories(e1_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_04 PUBLIC cxx_std_17)

# 相依上游：
#   - e1_03（逐像素 alpha surface）：命中測試需其 AlphaMode / AlphaProfile 表達 surface 的 alpha
#     命中語意（透明處不命中）。PUBLIC：本單元標頭 include alpha_surface.hpp，需傳遞 include 目錄。
#     （e1_03 已 PUBLIC 連 e1_21 + backend_null，故 null_backend.hpp 的 SurfaceId / SurfaceLayer /
#     HitPolicy 與 capability_matrix.hpp 之 include 目錄亦傳遞可得。）
target_link_libraries(e1_04 PUBLIC e1_03)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_04 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_04.cpp)
target_link_libraries(test_e1_04 PRIVATE e1_04 GTest::gtest_main)
add_test(NAME e1_04 COMMAND test_e1_04)

# ==== 單元 e1_05 ====
# src/kernel/e1_05/CMakeLists.txt — E1-05 具名碰撞區域 → 事件參數
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_05 STATIC named_region_map.cpp)
target_include_directories(e1_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_05 PUBLIC cxx_std_17)

# 相依上游：
#   - e1_04（幾何命中測試）：具名區域的命中判定建於其 HitTester / Shape / LocalPoint 之上。
#     PUBLIC：本單元標頭 include hit_test.hpp，需傳遞 include 目錄（連帶 e1_04 已 PUBLIC 連的
#     e1_03 / e1_21 / backend_null include 目錄亦傳遞可得）。
target_link_libraries(e1_05 PUBLIC e1_04)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_05 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_05.cpp)
target_link_libraries(test_e1_05 PRIVATE e1_05 GTest::gtest_main)
add_test(NAME e1_05 COMMAND test_e1_05)

# ==== 單元 e1_06 ====
# src/kernel/e1_06/CMakeLists.txt — E1-06 命中與繪製層序
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_06 STATIC paint_hit_order.cpp)
target_include_directories(e1_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_06 PUBLIC cxx_std_17)

# 相依上游 E1-04（幾何命中測試）：本單元標頭 include hit_test.hpp，且完全複用其
# HitTester::hit_test_alpha 做幾何 / alpha 判定，不重新實作。PUBLIC：需傳遞 include 目錄
# （e1_04 已 PUBLIC 連 e1_03，故 alpha_surface.hpp / null_backend.hpp 之 include 目錄亦傳遞可得）。
target_link_libraries(e1_06 PUBLIC e1_04)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_06 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_06.cpp)
target_link_libraries(test_e1_06 PRIVATE e1_06 GTest::gtest_main)
add_test(NAME e1_06 COMMAND test_e1_06)

# ==== 單元 e1_07 ====
# src/kernel/e1_07/CMakeLists.txt — E1-07 anchor 定位模型
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_07 STATIC anchor_model.cpp)
target_include_directories(e1_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_07 PUBLIC cxx_std_17)

# 相依上游 E1-03（可讀不可改）：承其 / E1-24 的具名 surface 模型（SurfaceId），
# AnchorLayout 以具名 SurfaceId 為鍵配對定位規格（NFR-02 具名指涉）。
# PUBLIC：本單元標頭 include null_backend.hpp（經 e1_03 傳遞 backend_null / e1_21 的 include 目錄）。
target_link_libraries(e1_07 PUBLIC e1_03)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_07 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_07.cpp)
target_link_libraries(test_e1_07 PRIVATE e1_07 GTest::gtest_main)
add_test(NAME e1_07 COMMAND test_e1_07)

# ==== 單元 e1_08 ====
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

# ==== 單元 e1_09 ====
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

# ==== 單元 e1_10 ====
# src/kernel/e1_10/CMakeLists.txt — E1-10 保持在螢幕內
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_10 STATIC keep_on_screen.cpp)
target_include_directories(e1_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_10 PUBLIC cxx_std_17)

# 相依上游（可讀不可改）：
#   - e1_07：AnchorLayout / resolve() 產出的 ResolvedPlacement、Size、is_finite_size，
#     本單元的夾回計算建於其定位結果之上。
#   - e1_18：ScreenId / ScreenRegistry 具名螢幕拓撲，KeepOnScreen 以其做具名螢幕存在性查詢。
# PUBLIC：本單元標頭 include anchor_model.hpp / named_screens.hpp，需傳遞其 include 目錄。
target_link_libraries(e1_10 PUBLIC e1_07 e1_18)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_10 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_10.cpp)
target_link_libraries(test_e1_10 PRIVATE e1_10 GTest::gtest_main)
add_test(NAME e1_10 COMMAND test_e1_10)

# ==== 單元 e1_11 ====
# src/kernel/e1_11/CMakeLists.txt — E1-11 從屬 surface 相對定位
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_11 STATIC subordinate_layout.cpp)
target_include_directories(e1_11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_11 PUBLIC cxx_std_17)

# 相依上游 E1-07（可讀不可改）：承其 Anchor / AnchorSpec / Size / ResolvedPlacement /
# AnchorStatus / resolve()，子 surface 以「父已解析矩形」作為 E1-07 resolve() 的 container
# 解析出相對佈局（NFR-02：具名 anchor + 相對偏移，無絕對座標）。
# PUBLIC：本單元標頭 include anchor_model.hpp（經 e1_07 傳遞 e1_03 / backend_null 的 include 目錄）。
target_link_libraries(e1_11 PUBLIC e1_07)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_11 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_11.cpp)
target_link_libraries(test_e1_11 PRIVATE e1_11 GTest::gtest_main)
add_test(NAME e1_11 COMMAND test_e1_11)

# ==== 單元 e1_12 ====
# src/kernel/e1_12/CMakeLists.txt — E1-12 多重從屬 surface 並存
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_12 STATIC multi_subordinate_manager.cpp)
target_include_directories(e1_12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_12 PUBLIC cxx_std_17)

# 相依上游 E1-11（可讀不可改）：承其 SubordinateLayout（父子附著 + 相對定位），本單元以
# 組合方式持有一個 SubordinateLayout 實例，加一層「多子並存」的列舉 / 批次操作管理。
# PUBLIC：本單元標頭 include subordinate_layout.hpp（經 e1_11 傳遞 e1_07 / e1_03 / e1_21 /
# backend_null 的 include 目錄）。
target_link_libraries(e1_12 PUBLIC e1_11)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_12 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_12.cpp)
target_link_libraries(test_e1_12 PRIVATE e1_12 GTest::gtest_main)
add_test(NAME e1_12 COMMAND test_e1_12)

# ==== 單元 e1_13 ====
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

# ==== 單元 e1_14 ====
# src/kernel/e1_14/CMakeLists.txt — E1-14 短暫 profile 生命週期
# 根 CMakeLists 以明確層級 GLOB（src/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_14 STATIC transient_profile.cpp)
target_include_directories(e1_14 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_14 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：
#   - e1_02（輸入策略四態）：本單元標頭 include `input_strategy.hpp`（InputStrategy /
#     kDefaultStrategy / to_backend_policy，並透傳 E1-24 SurfaceProfile / SurfaceLifecycle），
#     故 PUBLIC 傳遞其 include 目錄。
#   - e5_10（逾時計時器）：本單元標頭 include `timeout_timer.hpp`（TimeoutTimer / Tick /
#     TimerId，注入式邏輯計時），故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(e1_14 PUBLIC e1_02 e5_10)

# 單元測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_14 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_14.cpp)
target_link_libraries(test_e1_14 PRIVATE e1_14 GTest::gtest_main)
add_test(NAME e1_14 COMMAND test_e1_14)

# ==== 單元 e1_15 ====
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

# ==== 單元 e1_16 ====
# src/kernel/e1_16/CMakeLists.txt — E1-16 螢幕邊緣熱區
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_16 STATIC edge_hot_zone.cpp)
target_include_directories(e1_16 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_16 PUBLIC cxx_std_17)

# 相依上游：
#   - e1_04（幾何命中測試）：熱區矩形範圍的點內判定用其 HitTester / Shape / LocalPoint。
#     PUBLIC：本單元標頭 include hit_test.hpp，需傳遞 include 目錄。
target_link_libraries(e1_16 PUBLIC e1_04)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_16 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_16.cpp)
target_link_libraries(test_e1_16 PRIVATE e1_16 GTest::gtest_main)
add_test(NAME e1_16 COMMAND test_e1_16)

# ==== 單元 e1_17 ====
# src/kernel/e1_17/CMakeLists.txt — E1-17 DPI 感知與混合 DPI
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_17 STATIC dpi_awareness.cpp)
target_include_directories(e1_17 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_17 PUBLIC cxx_std_17)

# 契約測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_17 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_17.cpp)
target_link_libraries(test_e1_17 PRIVATE e1_17 GTest::gtest_main)
add_test(NAME e1_17 COMMAND test_e1_17)

# ==== 單元 e1_18 ====
# src/kernel/e1_18/CMakeLists.txt — E1-18 具名螢幕與每螢幕實例
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_18 STATIC named_screens.cpp)
target_include_directories(e1_18 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_18 PUBLIC cxx_std_17)

# 可與 E1-17 每螢幕縮放搭配（PerScreen<double> 保存各螢幕 scale_factor）。
# 本單元不強制依賴 E1-17；若上游 target 存在則連結，方便日後共用其型別。
if(TARGET e1_17)
    target_link_libraries(e1_18 PUBLIC e1_17)
endif()

# 契約測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_18 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_18.cpp)
target_link_libraries(test_e1_18 PRIVATE e1_18 GTest::gtest_main)
add_test(NAME e1_18 COMMAND test_e1_18)

# ==== 單元 e1_19 ====
# src/kernel/e1_19/CMakeLists.txt — E1-19 顯示器熱插拔復原
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_19 STATIC layout_recovery.cpp)
target_include_directories(e1_19 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_19 PUBLIC cxx_std_17)

# 上游相依（已合併，可讀不可改）：
#   e1_18 顯示器拓撲抽象（ScreenRegistry / PerScreen / ScreenId）—— 標頭 include named_screens.hpp
#   e5_08 系統事件（SystemEventSource / DisplayChanged）—— 標頭 include system_event.hpp
# 皆 PUBLIC：本單元標頭對外暴露這些型別，須把上游 include 目錄向下游傳遞。
target_link_libraries(e1_19 PUBLIC e1_18 e5_08)

# 契約測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_19 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_19.cpp)
target_link_libraries(test_e1_19 PRIVATE e1_19 GTest::gtest_main)
add_test(NAME e1_19 COMMAND test_e1_19)

# ==== 單元 e1_20 ====
# src/kernel/e1_20/CMakeLists.txt — E1-20 睡眠喚醒復原
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_20 STATIC sleep_wake_recovery.cpp)
target_include_directories(e1_20 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_20 PUBLIC cxx_std_17)

# 上游相依（已合併，可讀不可改）：
#   e1_01 具名圖層堆疊（LayerStack / SurfaceId / SurfaceLayer）—— 標頭 include layer_stack.hpp
#   e5_08 系統事件（SystemEventSource / SystemSleep / SystemWake）—— 標頭 include system_event.hpp
# 皆 PUBLIC：本單元標頭對外暴露這些型別，須把上游 include 目錄向下游傳遞。
target_link_libraries(e1_20 PUBLIC e1_01 e5_08)

# 契約測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_20 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_20.cpp)
target_link_libraries(test_e1_20 PRIVATE e1_20 GTest::gtest_main)
add_test(NAME e1_20 COMMAND test_e1_20)

# ==== 單元 e1_21 ====
# src/kernel/e1_21/CMakeLists.txt — E1-21 能力矩陣宣告檔
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_21 STATIC capability_matrix.cpp)
target_include_directories(e1_21 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_21 PUBLIC cxx_std_17)

# 契約測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_21 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_21.cpp)
target_link_libraries(test_e1_21 PRIVATE e1_21 GTest::gtest_main)
add_test(NAME e1_21 COMMAND test_e1_21)

# ==== 單元 e1_22 ====
# src/kernel/e1_22/CMakeLists.txt — E1-22 建置期能力閘控 lint
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_22 STATIC capability_lint.cpp)
target_include_directories(e1_22 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_22 PUBLIC cxx_std_17)

# 上游 E1-21 能力矩陣（可讀不可改）：NFR-03 預設閘控名單由其 optional 能力導出。
target_link_libraries(e1_22 PUBLIC e1_21)

# 契約測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_22 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_22.cpp)
target_link_libraries(test_e1_22 PRIVATE e1_22 GTest::gtest_main)
add_test(NAME e1_22 COMMAND test_e1_22)

# ==== 單元 e1_23 ====
# src/kernel/e1_23/CMakeLists.txt — E1-23 分層失敗的手動降級路徑
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_23 STATIC layer_fallback.cpp)
target_include_directories(e1_23 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_23 PUBLIC cxx_std_17)

# 上游相依（可讀不可改）：
#   - e1_01：LayerStack / LayerAssign / layer_capability()（本單元建於其上的手動降級路徑）。
#   - e1_21：CapabilityMatrix / has() 能力閘控（NFR-03；降級策略預設經此判斷可用性）。
# PUBLIC：本單元標頭 include 兩者標頭，故傳遞其 include 目錄。
target_link_libraries(e1_23 PUBLIC e1_01 e1_21)

# 契約 / 單元測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_23 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_23.cpp)
target_link_libraries(test_e1_23 PRIVATE e1_23 GTest::gtest_main)
add_test(NAME e1_23 COMMAND test_e1_23)
