# engine/script/e8_04/CMakeLists.txt — E8-04 行程內模組載入（擴充點 5 的載入機制）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e8_04 STATIC module_loader.cpp)
target_include_directories(e8_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e8_04 PUBLIC cxx_std_17)

# 相依 E9-02 manifest（已合併，可讀不可改）。PUBLIC：本單元 header 直接 #include
# manifest.hpp 並在對外 API（Module::manifest）中暴露 ds::package 型別。
target_link_libraries(e8_04 PUBLIC e9_02)

# 契約測試在 tests/e8/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e8_04 ${CMAKE_SOURCE_DIR}/tests/e8/test_e8_04.cpp)
target_link_libraries(test_e8_04 PRIVATE e8_04 GTest::gtest_main)
add_test(NAME e8_04 COMMAND test_e8_04)
