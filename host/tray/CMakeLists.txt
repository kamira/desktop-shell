# H1-02 widget 控制命令 + 托盤選單裝配
if(WIN32)
  add_library(h1_02_controls STATIC widget_controls.cpp)
  target_include_directories(h1_02_controls PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(h1_02_controls PUBLIC w1_01 e11_01 e6_01)
  target_compile_features(h1_02_controls PUBLIC cxx_std_17)

  # 整合測試：走完「選單項 → E6-01 命令 → 後端狀態真的改變」整條鏈，
  # 只留「滑鼠實際點在托盤圖示上」那一步給操作驗收。
  add_executable(test_h1_02 ${CMAKE_SOURCE_DIR}/tests/h1/test_h1_02.cpp)
  target_link_libraries(test_h1_02 PRIVATE h1_02_controls w1_02 GTest::gtest_main)
  add_test(NAME h1_02_controls COMMAND test_h1_02)
endif()
