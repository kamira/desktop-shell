# src/host/e11_03/CMakeLists.txt — E11-03 開機自動啟動
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e11_03 STATIC autostart.cpp)
target_include_directories(e11_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e11_03 PUBLIC cxx_std_17)

# 契約測試在 tests/e11/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e11_03 ${CMAKE_SOURCE_DIR}/tests/e11/test_e11_03.cpp)
target_link_libraries(test_e11_03 PRIVATE e11_03 GTest::gtest_main)
add_test(NAME e11_03 COMMAND test_e11_03)
