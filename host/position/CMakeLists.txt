# H1-03 拖曳裝配 + 位置持久化
if(WIN32)
  add_library(h1_03_position STATIC position_store.cpp)
  target_include_directories(h1_03_position PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # 消費 W1-01/W1-03（win32 後端）、E1-08（拖曳狀態機與位置記憶，透遞 E1-07 錨點模型）。
  target_link_libraries(h1_03_position PUBLIC w1_01 e1_08)
  target_compile_features(h1_03_position PUBLIC cxx_std_17)

  add_executable(test_h1_03 ${CMAKE_SOURCE_DIR}/tests/h1/test_h1_03.cpp)
  target_link_libraries(test_h1_03 PRIVATE h1_03_position GTest::gtest_main)
  add_test(NAME h1_03_position COMMAND test_h1_03)
endif()
