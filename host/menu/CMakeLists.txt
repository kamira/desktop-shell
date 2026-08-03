# W1-05 win32 自繪選單呈現
#
# 與其他 win32 相關單元同理，整份 if(WIN32) 守衛。
if(WIN32)
  add_library(w1_05_menu STATIC owner_drawn_menu.cpp)
  target_include_directories(w1_05_menu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # 消費 E11-02（自繪選單模型 / 排版 / 巡覽）、E11-01（TrayMenu）、E7-13（Item 森林）。
  target_link_libraries(w1_05_menu PUBLIC e11_02 e11_01 e7_13)
  target_compile_features(w1_05_menu PUBLIC cxx_std_17)

  add_executable(test_w1_05 ${CMAKE_SOURCE_DIR}/tests/e11/test_owner_drawn_menu.cpp)
  target_link_libraries(test_w1_05 PRIVATE w1_05_menu GTest::gtest_main)
  add_test(NAME owner_drawn_menu COMMAND test_w1_05)
endif()
