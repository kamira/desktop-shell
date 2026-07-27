# modules/actuators/e3_08/CMakeLists.txt — E3-08 網路 / 藍牙開關致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_08 STATIC radio_actuator.cpp)
target_include_directories(e3_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_08 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（已合併，可讀不可改）：八個具名命令掛上匯流排、參數重用其穩定值型別。
# PUBLIC 以傳遞 e6_01 的 include 目錄給本 target 與其消費者（測試）。
target_link_libraries(e3_08 PUBLIC e6_01)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_08 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_08.cpp)
target_link_libraries(test_e3_08 PRIVATE e3_08 GTest::gtest_main)
add_test(NAME e3_08 COMMAND test_e3_08)
