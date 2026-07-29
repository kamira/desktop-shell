# src/kernel/backend/null/CMakeLists.txt — E1-24 null 後端參考實作
#
# 根 CMakeLists 以深度 3 的 GLOB（src/*/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
# null 後端刻意留在 src/kernel/backend/ 下（後端家族不拆散；backend_guard 以此路徑執行相位閘門）。
add_library(backend_null STATIC null_backend.cpp)
target_include_directories(backend_null PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(backend_null PUBLIC cxx_std_17)

# 相依上游 E1-21 能力矩陣（PUBLIC：本後端標頭 include 其標頭，故傳遞 include 目錄）。
target_link_libraries(backend_null PUBLIC e1_21)

# 契約 / 單元測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_backend_null ${CMAKE_SOURCE_DIR}/tests/e1/test_backend_null.cpp)
target_link_libraries(test_backend_null PRIVATE backend_null GTest::gtest_main)
add_test(NAME backend_null COMMAND test_backend_null)
