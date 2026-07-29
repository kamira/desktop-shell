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
