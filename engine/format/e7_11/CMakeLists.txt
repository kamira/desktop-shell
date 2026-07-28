# engine/format/e7_11/CMakeLists.txt — E7-11 群組與批次操作（engine 層，平台中立）
# 依賴上游 E7-01（ds::format::Value）與 E6-01（ds::command::CommandBus）。
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e7_11 STATIC group.cpp)
target_include_directories(e7_11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e7_11 PUBLIC cxx_std_17)

# 上游已合併，可讀不可改；PUBLIC 連結使消費者一併取得其 include 目錄。
target_link_libraries(e7_11 PUBLIC e7_01 e6_01)

# 契約測試在 tests/e7/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e7_11 ${CMAKE_SOURCE_DIR}/tests/e7/test_e7_11.cpp)
target_link_libraries(test_e7_11 PRIVATE e7_11 GTest::gtest_main)
add_test(NAME e7_11 COMMAND test_e7_11)
