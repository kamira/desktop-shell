# engine/command/e6_03/CMakeLists.txt — E6-03 條件動作與去抖
# 於 E6-01 命令匯流排之上加條件動作與去抖 / 節流；條件以 E7-05 公式求值。
# 根 CMakeLists 會自動 glob 收入本檔（engine/*/*/CMakeLists.txt，見 docs/backlog/BUILD.md）。
add_library(e6_03 STATIC conditional_action.cpp)
target_include_directories(e6_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e6_03 PUBLIC cxx_std_17)

# 消費 E6-01（命令匯流排 / 分派）與 E7-05（公式求值引擎）；兩者已合併，可讀不可改。
# PUBLIC 讓下游一併取得其標頭（本單元的公開介面型別引用了兩者）。
target_link_libraries(e6_03 PUBLIC e6_01 e7_05)

# 契約測試在 tests/e6/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e6_03 ${CMAKE_SOURCE_DIR}/tests/e6/test_e6_03.cpp)
target_link_libraries(test_e6_03 PRIVATE e6_03 GTest::gtest_main)
add_test(NAME e6_03 COMMAND test_e6_03)
