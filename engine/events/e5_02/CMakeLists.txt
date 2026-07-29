# engine/events/e5_02/CMakeLists.txt — E5-02 懸停進出事件
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e5_02 STATIC hover_tracker.cpp)
target_include_directories(e5_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e5_02 PUBLIC cxx_std_17)

# 相依上游：E1-04 幾何命中測試（HitTester / HitSurface / LocalPoint / TopmostHit）。
# PUBLIC：本單元標頭 include hit_test.hpp，需傳遞其 include 目錄（e1_04 已 PUBLIC 連 e1_03，
# 故 alpha_surface.hpp / null_backend.hpp 的 include 目錄亦傳遞可得）。
target_link_libraries(e5_02 PUBLIC e1_04)

# 測試檔在 tests/e5/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e5_02 ${CMAKE_SOURCE_DIR}/tests/e5/test_e5_02.cpp)
target_link_libraries(test_e5_02 PRIVATE e5_02 GTest::gtest_main)
add_test(NAME e5_02 COMMAND test_e5_02)
