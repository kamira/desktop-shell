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
