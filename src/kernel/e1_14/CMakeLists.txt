# src/kernel/e1_14/CMakeLists.txt — E1-14 短暫 profile 生命週期
# 根 CMakeLists 以明確層級 GLOB（src/*/*/CMakeLists.txt）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_14 STATIC transient_profile.cpp)
target_include_directories(e1_14 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_14 PUBLIC cxx_std_17)

# 上游相依（皆已合併，可讀不可改）：
#   - e1_02（輸入策略四態）：本單元標頭 include `input_strategy.hpp`（InputStrategy /
#     kDefaultStrategy / to_backend_policy，並透傳 E1-24 SurfaceProfile / SurfaceLifecycle），
#     故 PUBLIC 傳遞其 include 目錄。
#   - e5_10（逾時計時器）：本單元標頭 include `timeout_timer.hpp`（TimeoutTimer / Tick /
#     TimerId，注入式邏輯計時），故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(e1_14 PUBLIC e1_02 e5_10)

# 單元測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_14 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_14.cpp)
target_link_libraries(test_e1_14 PRIVATE e1_14 GTest::gtest_main)
add_test(NAME e1_14 COMMAND test_e1_14)
