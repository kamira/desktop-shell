# engine/script/e8_01/CMakeLists.txt — E8-01 腳本引擎（QML/JS）
# 描述子系統的腳本引擎抽象與嵌入介面（engine 層，平台中立）。根 CMakeLists 會自動 glob 收入本檔。
add_library(e8_01 STATIC script_engine.cpp)
target_include_directories(e8_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e8_01 PUBLIC cxx_std_17)

# 消費 E6-01 的命令匯流排（ScriptValue 沿用其 CommandValue；命令橋接分派 CommandBus）；
# 已合併，可讀不可改。PUBLIC 讓下游一併取得其標頭。
target_link_libraries(e8_01 PUBLIC e6_01)

# 契約測試在 tests/e8/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e8_01 ${CMAKE_SOURCE_DIR}/tests/e8/test_e8_01.cpp)
target_link_libraries(test_e8_01 PRIVATE e8_01 GTest::gtest_main)
add_test(NAME e8_01 COMMAND test_e8_01)
