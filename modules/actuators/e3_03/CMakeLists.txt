# modules/actuators/e3_03/CMakeLists.txt — E3-03 媒體播放控制致動器
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md，modules/*/*/CMakeLists.txt）。
add_library(e3_03 STATIC media_control_actuator.cpp)
target_include_directories(e3_03 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e3_03 PUBLIC cxx_std_17)

# 相依 E6-01 命令匯流排（掛具名命令、重用穩定值型別）與 E2-13 媒體中繼資料
# （重用平台中立播放狀態詞彙 PlaybackState / state_rank / to_string，與讀取端對稱）。
# 皆已合併，可讀不可改。PUBLIC：本單元對外標頭 include 了 command_bus.hpp 與
# media_metadata.hpp，須把兩上游的 include 目錄向下游（測試）傳遞。
target_link_libraries(e3_03 PUBLIC e6_01 e2_13)

# 測試檔在 tests/e3/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e3_03 ${CMAKE_SOURCE_DIR}/tests/e3/test_e3_03.cpp)
target_link_libraries(test_e3_03 PRIVATE e3_03 GTest::gtest_main)
add_test(NAME e3_03 COMMAND test_e3_03)
