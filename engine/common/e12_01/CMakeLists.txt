# engine/common/e12_01/CMakeLists.txt — E12-01 idle 記憶體與 CPU 門檻
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e12_01 STATIC idle_threshold.cpp)
target_include_directories(e12_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e12_01 PUBLIC cxx_std_17)

# 相依 E2-02 採集頻率分級與除頻（沿用 SamplingTier 作採集分級身分，閒置降級即在其上操作）。
# PUBLIC：本單元公開標頭 include 了 sampling.hpp，故傳遞相依給下游消費者。
target_link_libraries(e12_01 PUBLIC e2_02)

# 契約測試在 tests/e12/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e12_01 ${CMAKE_SOURCE_DIR}/tests/e12/test_e12_01.cpp)
target_link_libraries(test_e12_01 PRIVATE e12_01 GTest::gtest_main)
add_test(NAME e12_01 COMMAND test_e12_01)
