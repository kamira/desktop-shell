# engine/package/e9_04/CMakeLists.txt — E9-04 主題切換
# 視覺主題的註冊 / 執行期切換 / 變更通知（engine 層，平台中立）。
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_04 STATIC theme.cpp)
target_include_directories(e9_04 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_04 PUBLIC cxx_std_17)

# 消費 E9-03（可互換元件組合，經其再傳遞 E9-01 / E9-02）與 E7-07（熱重載，經其再傳遞
# E7-01 / E7-06）。PUBLIC 讓下游一併取得其標頭。上游已合併，可讀不可改。
target_link_libraries(e9_04 PUBLIC e9_03 e7_07)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_04 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_04.cpp)
target_link_libraries(test_e9_04 PRIVATE e9_04 GTest::gtest_main)
add_test(NAME e9_04 COMMAND test_e9_04)
