# W1-01 win32 kernel 後端
#
# **整份以 if(WIN32) 包住**：本後端含真實 win32 API，在 ubuntu / macOS 編不過。
# CI（G3）目前跑 ubuntu，若不守衛則整個 build 紅燈。守衛之後 ubuntu 完全看不到這些目標，
# 既有 176 個測試照跑不誤。
#
# 代價要說清楚：這也意味著 **win32 後端的正確性目前不在任何閘門的守備範圍內**——
# 它只在本機 Windows 上被建置與測試過。要讓它進閘門需加 windows runner
# （docs/backlog/phase2-desktop-runtime.md §6 checklist 步驟 3）。見 CHG-20260803-03 已知限制。
if(WIN32)
  add_library(w1_01 STATIC win32_backend.cpp)
  target_include_directories(w1_01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # 消費 E1-24（KernelBackend 介面 + 具名列舉）與 E1-21（能力矩陣），皆上游、可讀不可改。
  target_link_libraries(w1_01 PUBLIC backend_null e1_21)
  target_compile_features(w1_01 PUBLIC cxx_std_17)

  add_executable(test_backend_win32 ${CMAKE_SOURCE_DIR}/tests/e1/test_backend_win32.cpp)
  target_link_libraries(test_backend_win32 PRIVATE w1_01 GTest::gtest_main)
  add_test(NAME backend_win32 COMMAND test_backend_win32)
endif()
