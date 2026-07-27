# src/kernel/e1_18/CMakeLists.txt — E1-18 具名螢幕與每螢幕實例
# 根 CMakeLists 會自動 glob 收入本檔（見 docs/backlog/BUILD.md）。
add_library(e1_18 STATIC named_screens.cpp)
target_include_directories(e1_18 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(e1_18 PUBLIC cxx_std_17)

# 可與 E1-17 每螢幕縮放搭配（PerScreen<double> 保存各螢幕 scale_factor）。
# 本單元不強制依賴 E1-17；若上游 target 存在則連結，方便日後共用其型別。
if(TARGET e1_17)
    target_link_libraries(e1_18 PUBLIC e1_17)
endif()

# 契約測試在 tests/e1/（也是本單元的 write_scope），以絕對來源路徑指向。
add_executable(test_e1_18 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_18.cpp)
target_link_libraries(test_e1_18 PRIVATE e1_18 GTest::gtest_main)
add_test(NAME e1_18 COMMAND test_e1_18)
