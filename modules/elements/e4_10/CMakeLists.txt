# modules/elements/e4_10/CMakeLists.txt — E4-10 閒置自動動畫觸發
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e4_10 STATIC idle_animation_trigger.cpp)
target_include_directories(e4_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e4_10 PUBLIC cxx_std_17)

# 相依上游（已合併，可讀不可改）：
#   - e4_07：幀序列動畫（FrameAnimationElement）。
#   - e5_09：機率排程器（ProbabilisticScheduler / RandomSource）。
# PUBLIC：本單元標頭 include 上游標頭，需傳遞其 include 目錄給下游。
target_link_libraries(e4_10 PUBLIC e4_07 e5_09)

# 測試檔在 tests/e4/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e4_10 ${CMAKE_SOURCE_DIR}/tests/e4/test_e4_10.cpp)
target_link_libraries(test_e4_10 PRIVATE e4_10 GTest::gtest_main)
add_test(NAME e4_10 COMMAND test_e4_10)
