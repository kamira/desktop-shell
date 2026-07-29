# engine/package/e9_09/CMakeLists.txt — E9-09 動態配色生成
# 從種子色動態生成一整套協調配色（主/次/強調/背景/前景/狀態色），橋接 E9-04 主題。
# engine 層、平台中立、純邏輯。根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e9_09 STATIC palette.cpp)
target_include_directories(e9_09 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e9_09 PUBLIC cxx_std_17)

# 消費 E9-04（主題切換 / ThemeData；經其再傳遞 E9-03 / E7-07 → E7-01 等）。
# PUBLIC 讓下游一併取得其標頭。上游已合併，可讀不可改。
target_link_libraries(e9_09 PUBLIC e9_04)

# 契約測試在 tests/e9/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e9_09 ${CMAKE_SOURCE_DIR}/tests/e9/test_e9_09.cpp)
target_link_libraries(test_e9_09 PRIVATE e9_09 GTest::gtest_main)
add_test(NAME e9_09 COMMAND test_e9_09)
