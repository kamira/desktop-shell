# src/kernel/e1_10/CMakeLists.txt — E1-10 保持在螢幕內
# 根 CMakeLists 以層級 GLOB（src/*/*/CMakeLists.txt，深度 2）自動收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_10 STATIC keep_on_screen.cpp)
target_include_directories(e1_10 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_10 PUBLIC cxx_std_17)

# 相依上游（可讀不可改）：
#   - e1_07：AnchorLayout / resolve() 產出的 ResolvedPlacement、Size、is_finite_size，
#     本單元的夾回計算建於其定位結果之上。
#   - e1_18：ScreenId / ScreenRegistry 具名螢幕拓撲，KeepOnScreen 以其做具名螢幕存在性查詢。
# PUBLIC：本單元標頭 include anchor_model.hpp / named_screens.hpp，需傳遞其 include 目錄。
target_link_libraries(e1_10 PUBLIC e1_07 e1_18)

# 測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_10 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_10.cpp)
target_link_libraries(test_e1_10 PRIVATE e1_10 GTest::gtest_main)
add_test(NAME e1_10 COMMAND test_e1_10)
