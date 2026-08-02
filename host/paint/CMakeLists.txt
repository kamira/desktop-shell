# H1-01（其一）render_model → GDI 繪製橋接
#
# 與 W1-01 同理，整份以 if(WIN32) 包住：含真實 GDI 呼叫，ubuntu 編不過。
if(WIN32)
  add_library(h1_01_paint STATIC widget_painter.cpp)
  target_include_directories(h1_01_paint PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # 消費 C2-02（SystemStatusRenderModel）與其透遞的 E4-03 / E4-01 / E7-*，皆上游可讀不可改。
  target_link_libraries(h1_01_paint PUBLIC c2_02)
  target_compile_features(h1_01_paint PUBLIC cxx_std_17)

  add_executable(test_h1_01 ${CMAKE_SOURCE_DIR}/tests/h1/test_h1_01.cpp)
  target_link_libraries(test_h1_01 PRIVATE h1_01_paint GTest::gtest_main)
  add_test(NAME h1_01_paint COMMAND test_h1_01)
endif()
