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
