# modules/sysinfo/CMakeLists.txt — 子系統合併建置定義（CHG-20260730-03 目錄扁平化）
# 原 per-unit <slug>/ 層已去除，各單元源碼直接置於本目錄；
# 各單元的 add_library / test target 定義於下逐一保留（裸檔名、target 各自獨立）。
# 根 CMakeLists 以扁平深度 GLOB 收入本檔。

# ==== 單元 e2_03 ====
# modules/sysinfo/e2_03/CMakeLists.txt — E2-03 CPU 負載（總體與每核心）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_03 STATIC cpu_load.cpp)
target_include_directories(e2_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_03 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_03 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_03 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_03.cpp)
target_link_libraries(test_e2_03 PRIVATE e2_03 GTest::gtest_main)
add_test(NAME e2_03 COMMAND test_e2_03)

# ==== 單元 e2_04 ====
# modules/sysinfo/e2_04/CMakeLists.txt — E2-04 記憶體使用量（實體/交換）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_04 STATIC memory_usage.cpp)
target_include_directories(e2_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_04 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_04 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_04 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_04.cpp)
target_link_libraries(test_e2_04 PRIVATE e2_04 GTest::gtest_main)
add_test(NAME e2_04 COMMAND test_e2_04)

# ==== 單元 e2_05 ====
# modules/sysinfo/e2_05/CMakeLists.txt — E2-05 GPU 使用率 / VRAM / 溫度
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_05 STATIC gpu_stats.cpp)
target_include_directories(e2_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_05 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_05 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_05 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_05.cpp)
target_link_libraries(test_e2_05 PRIVATE e2_05 GTest::gtest_main)
add_test(NAME e2_05 COMMAND test_e2_05)

# ==== 單元 e2_06 ====
# modules/sysinfo/e2_06/CMakeLists.txt — E2-06 儲存容量
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_06 STATIC storage_capacity.cpp)
target_include_directories(e2_06 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_06 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_06 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_06 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_06.cpp)
target_link_libraries(test_e2_06 PRIVATE e2_06 GTest::gtest_main)
add_test(NAME e2_06 COMMAND test_e2_06)

# ==== 單元 e2_07 ====
# modules/sysinfo/e2_07/CMakeLists.txt — E2-07 儲存 IO 吞吐與佇列
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_07 STATIC disk_io.cpp)
target_include_directories(e2_07 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_07 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_07 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_07 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_07.cpp)
target_link_libraries(test_e2_07 PRIVATE e2_07 GTest::gtest_main)
add_test(NAME e2_07 COMMAND test_e2_07)

# ==== 單元 e2_08 ====
# modules/sysinfo/e2_08/CMakeLists.txt — E2-08 網路流量
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_08 STATIC network_traffic.cpp)
target_include_directories(e2_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_08 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_08 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_08 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_08.cpp)
target_link_libraries(test_e2_08 PRIVATE e2_08 GTest::gtest_main)
add_test(NAME e2_08 COMMAND test_e2_08)

# ==== 單元 e2_09 ====
# modules/sysinfo/e2_09/CMakeLists.txt — E2-09 電源與電池狀態
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_09 STATIC power_status.cpp)
target_include_directories(e2_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_09 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_09 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_09 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_09.cpp)
target_link_libraries(test_e2_09 PRIVATE e2_09 GTest::gtest_main)
add_test(NAME e2_09 COMMAND test_e2_09)

# ==== 單元 e2_10 ====
# modules/sysinfo/e2_10/CMakeLists.txt — E2-10 時間與日期
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_10 STATIC time_date.cpp)
target_include_directories(e2_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_10 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面契約（上游已合併，可讀不可改）。
# PUBLIC：本單元對外的標頭 include 了 metric.hpp，須把 e2_01 的 include 目錄向下游傳遞。
target_link_libraries(e2_10 PUBLIC e2_01)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_10 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_10.cpp)
target_link_libraries(test_e2_10 PRIVATE e2_10 GTest::gtest_main)
add_test(NAME e2_10 COMMAND test_e2_10)

# ==== 單元 e2_11 ====
# modules/sysinfo/e2_11/CMakeLists.txt — E2-11 系統運行時間
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_11 STATIC uptime.cpp)
target_include_directories(e2_11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_11 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_11 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_11 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_11.cpp)
target_link_libraries(test_e2_11 PRIVATE e2_11 GTest::gtest_main)
add_test(NAME e2_11 COMMAND test_e2_11)

# ==== 單元 e2_12 ====
# modules/sysinfo/e2_12/CMakeLists.txt — E2-12 系統靜態資訊
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_12 STATIC system_info.cpp)
target_include_directories(e2_12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_12 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面契約（上游已合併，可讀不可改）。
# PUBLIC：本單元對外的標頭 include 了 metric.hpp，須把 e2_01 的 include 目錄向下游傳遞。
target_link_libraries(e2_12 PUBLIC e2_01)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_12 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_12.cpp)
target_link_libraries(test_e2_12 PRIVATE e2_12 GTest::gtest_main)
add_test(NAME e2_12 COMMAND test_e2_12)

# ==== 單元 e2_13 ====
# modules/sysinfo/e2_13/CMakeLists.txt — E2-13 媒體播放中繼資料
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_13 STATIC media_metadata.cpp)
target_include_directories(e2_13 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_13 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_13 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_13 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_13.cpp)
target_link_libraries(test_e2_13 PRIVATE e2_13 GTest::gtest_main)
add_test(NAME e2_13 COMMAND test_e2_13)

# ==== 單元 e2_14 ====
# modules/sysinfo/e2_14/CMakeLists.txt — E2-14 HTTP 取得與結構化解析
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_14 STATIC http_fetch.cpp)
target_include_directories(e2_14 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_14 PUBLIC cxx_std_17)

# 消費上游契約（皆已合併，可讀不可改）：
#   e2_01 統一指標介面（MetricProvider / MetricRegistry / InMemoryMetric …）
#   e2_02 採集頻率分級（SamplingTier / SamplingScheduler / DemandId）
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩者的 include 目錄
# 向下游傳遞。
target_link_libraries(e2_14 PUBLIC e2_01 e2_02)

# 契約測試在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_14 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_14.cpp)
target_link_libraries(test_e2_14 PRIVATE e2_14 GTest::gtest_main)
add_test(NAME e2_14 COMMAND test_e2_14)

# ==== 單元 e2_15 ====
# modules/sysinfo/e2_15/CMakeLists.txt — E2-15 音訊峰值與頻譜
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_15 STATIC audio_levels.cpp)
target_include_directories(e2_15 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_15 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_15 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_15 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_15.cpp)
target_link_libraries(test_e2_15 PRIVATE e2_15 GTest::gtest_main)
add_test(NAME e2_15 COMMAND test_e2_15)

# ==== 單元 e2_16 ====
# modules/sysinfo/e2_16/CMakeLists.txt — E2-16 已安裝應用列舉
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_16 STATIC installed_apps.cpp)
target_include_directories(e2_16 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_16 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面契約（上游已合併，可讀不可改）。
# PUBLIC：本單元對外的標頭 include 了 metric.hpp，須把 e2_01 的 include 目錄向下游傳遞。
target_link_libraries(e2_16 PUBLIC e2_01)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_16 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_16.cpp)
target_link_libraries(test_e2_16 PRIVATE e2_16 GTest::gtest_main)
add_test(NAME e2_16 COMMAND test_e2_16)

# ==== 單元 e2_17 ====
# modules/sysinfo/e2_17/CMakeLists.txt — E2-17 主機板感測器（溫度 / 風扇 / 電壓）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_17 STATIC board_sensors.cpp)
target_include_directories(e2_17 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_17 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_17 PUBLIC e2_01 e2_02)

# 宣告相依 E10-01（本機 IPC 訊息投遞）：E10-01 為 IPC 通道、**非**感測器 / 裝置列舉
# 基礎設施，本單元感測器列舉自帶可注入抽象、不 include 其標頭；此處以 PRIVATE 連結以
# 忠實反映 units.json 宣告的相依邊，而不對下游傳遞其 include 目錄。
target_link_libraries(e2_17 PRIVATE e10_01)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_17 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_17.cpp)
target_link_libraries(test_e2_17 PRIVATE e2_17 GTest::gtest_main)
add_test(NAME e2_17 COMMAND test_e2_17)

# ==== 單元 e2_18 ====
# modules/sysinfo/e2_18/CMakeLists.txt — E2-18 效能計數器（任意）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_18 STATIC perf_counter.cpp)
target_include_directories(e2_18 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_18 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_18 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_18 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_18.cpp)
target_link_libraries(test_e2_18 PRIVATE e2_18 GTest::gtest_main)
add_test(NAME e2_18 COMMAND test_e2_18)

# ==== 單元 e2_19 ====
# modules/sysinfo/e2_19/CMakeLists.txt — E2-19 行程監看
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_19 STATIC process_monitor.cpp)
target_include_directories(e2_19 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_19 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_19 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_19 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_19.cpp)
target_link_libraries(test_e2_19 PRIVATE e2_19 GTest::gtest_main)
add_test(NAME e2_19 COMMAND test_e2_19)

# ==== 單元 e2_20 ====
# modules/sysinfo/e2_20/CMakeLists.txt — E2-20 網路連通性
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_20 STATIC connectivity.cpp)
target_include_directories(e2_20 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_20 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_20 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_20 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_20.cpp)
target_link_libraries(test_e2_20 PRIVATE e2_20 GTest::gtest_main)
add_test(NAME e2_20 COMMAND test_e2_20)

# ==== 單元 e2_21 ====
# modules/sysinfo/e2_21/CMakeLists.txt — E2-21 使用者文字輸入值
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_21 STATIC user_text_input.cpp)
target_include_directories(e2_21 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_21 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面契約 + E4-15 就地輸入框（皆上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 user_text_input.hpp 同時 include 了 metric.hpp 與
# text_input_element.hpp，須把兩者的 include 目錄向下游傳遞。
target_link_libraries(e2_21 PUBLIC e2_01 e4_15)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_21 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_21.cpp)
target_link_libraries(test_e2_21 PRIVATE e2_21 GTest::gtest_main)
add_test(NAME e2_21 COMMAND test_e2_21)

# ==== 單元 e2_22 ====
# modules/sysinfo/e2_22/CMakeLists.txt — E2-22 登錄檔讀取
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_22 STATIC registry_read.cpp)
target_include_directories(e2_22 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_22 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面契約（上游已合併，可讀不可改）。
# PUBLIC：本單元對外的標頭 include 了 metric.hpp，須把 e2_01 的 include 目錄向下游傳遞。
target_link_libraries(e2_22 PUBLIC e2_01)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_22 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_22.cpp)
target_link_libraries(test_e2_22 PRIVATE e2_22 GTest::gtest_main)
add_test(NAME e2_22 COMMAND test_e2_22)

# ==== 單元 e2_23 ====
# modules/sysinfo/e2_23/CMakeLists.txt — E2-23 檔案系統列舉
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_23 STATIC fs_enum.cpp)
target_include_directories(e2_23 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_23 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面契約（上游已合併，可讀不可改）。
# PUBLIC：本單元對外的標頭 include 了 metric.hpp，須把 e2_01 的 include 目錄向下游傳遞。
target_link_libraries(e2_23 PUBLIC e2_01)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_23 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_23.cpp)
target_link_libraries(test_e2_23 PRIVATE e2_23 GTest::gtest_main)
add_test(NAME e2_23 COMMAND test_e2_23)

# ==== 單元 e2_24 ====
# modules/sysinfo/e2_24/CMakeLists.txt — E2-24 Wi-Fi 狀態
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_24 STATIC wifi_status.cpp)
target_include_directories(e2_24 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_24 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集頻率分級（上游已合併，可讀不可改）。
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩上游的
# include 目錄向下游傳遞。
target_link_libraries(e2_24 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_24 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_24.cpp)
target_link_libraries(test_e2_24 PRIVATE e2_24 GTest::gtest_main)
add_test(NAME e2_24 COMMAND test_e2_24)

# ==== 單元 e2_25 ====
# modules/sysinfo/e2_25/CMakeLists.txt — E2-25 剪貼簿監看
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_25 STATIC clipboard_monitor.cpp)
target_include_directories(e2_25 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_25 PUBLIC cxx_std_17)

# 消費上游契約（皆已合併，可讀不可改）：
#   e2_01 統一指標介面（MetricProvider / MetricRegistry / InMemoryMetric …）
#   e2_02 採集頻率分級（SamplingTier / SamplingScheduler / DemandId）
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 sampling.hpp，須把兩者的 include 目錄
# 向下游傳遞。
target_link_libraries(e2_25 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_25 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_25.cpp)
target_link_libraries(test_e2_25 PRIVATE e2_25 GTest::gtest_main)
add_test(NAME e2_25 COMMAND test_e2_25)

# ==== 單元 e2_26 ====
# modules/sysinfo/e2_26/CMakeLists.txt — E2-26 RSS / 標題抓取
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_26 STATIC feed_fetch.cpp)
target_include_directories(e2_26 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_26 PUBLIC cxx_std_17)

# 消費上游契約（皆已合併，可讀不可改）：
#   e2_01 統一指標介面（MetricProvider / MetricRegistry / InMemoryMetric …）
#   e2_14 HTTP 取得（HttpTransport / NullHttpTransport / HttpResponse）
# PUBLIC：本單元對外標頭 include 了 metric.hpp 與 http_fetch.hpp，須把兩者的 include 目錄
# 向下游傳遞。（e2_14 又 PUBLIC 相依 e2_02，透過 e2_14 傳遞。）
target_link_libraries(e2_26 PUBLIC e2_01 e2_14)

# 契約測試在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_26 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_26.cpp)
target_link_libraries(test_e2_26 PRIVATE e2_26 GTest::gtest_main)
add_test(NAME e2_26 COMMAND test_e2_26)

# ==== 單元 e2_27 ====
# modules/sysinfo/e2_27/CMakeLists.txt — E2-27 螢幕像素取樣
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e2_27 STATIC screen_pixel.cpp)
target_include_directories(e2_27 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e2_27 PUBLIC cxx_std_17)

# 消費 E2-01 統一指標介面 + E2-02 採集分級（上游已合併，可讀不可改）。
# PUBLIC：本單元標頭 include 了 metric.hpp 與 sampling.hpp，須把上游的 include 目錄
# 向下游傳遞。
target_link_libraries(e2_27 PUBLIC e2_01 e2_02)

# 測試檔在 tests/e2/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e2_27 ${CMAKE_SOURCE_DIR}/tests/e2/test_e2_27.cpp)
target_link_libraries(test_e2_27 PRIVATE e2_27 GTest::gtest_main)
add_test(NAME e2_27 COMMAND test_e2_27)
