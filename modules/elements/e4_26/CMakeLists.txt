# modules/elements/e4_26/CMakeLists.txt — E4-26 音效播放
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_26 STATIC audio_player.cpp)
target_include_directories(e4_26 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_26 PUBLIC cxx_std_17)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_26 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_26.cpp)
target_link_libraries(test_e4_26 PRIVATE e4_26 GTest::gtest_main)
add_test(NAME e4_26 COMMAND test_e4_26)
