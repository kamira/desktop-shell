# modules/actuators/e3_04/CMakeLists.txt — E3-04 電源動作致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_04 STATIC power_actuator.cpp)
target_include_directories(e3_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_04 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：五個具名電源命令掛上匯流排、結果重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_04 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_04 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_04.cpp)
target_link_libraries(test_e3_04 PRIVATE e3_04 GTest::gtest_main)
add_test(NAME e3_04 COMMAND test_e3_04)
