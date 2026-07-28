# engine/script/e8_05/CMakeLists.txt — E8-05 外部模組開發介面（SDK；擴充點 5 的開發面）
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e8_05 STATIC module_sdk.cpp)
target_include_directories(e8_05 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e8_05 PUBLIC cxx_std_17)

# 相依 E8-04 載入機制（已合併，可讀不可改）。PUBLIC：本單元 header 直接 #include
# module_loader.hpp 並在對外 API（make_module → ds::ext::Module、ModuleContext 橋接
# ds::ext::ModuleContext）中暴露 ds::ext 型別。E9-02 manifest 型別經 e8_04 PUBLIC 傳遞。
target_link_libraries(e8_05 PUBLIC e8_04)

# 契約測試在 tests/e8/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e8_05 ${CMAKE_SOURCE_DIR}/tests/e8/test_e8_05.cpp)
target_link_libraries(test_e8_05 PRIVATE e8_05 GTest::gtest_main)
add_test(NAME e8_05 COMMAND test_e8_05)
