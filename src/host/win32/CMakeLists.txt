# W1-02 win32 系統匣後端
#
# 與 W1-01 / H1-01 同理，整份 if(WIN32) 守衛：含 Shell_NotifyIcon / TrackPopupMenu，
# ubuntu 編不過。代價同樣是本單元不在 CI 閘門守備範圍內（見 CHG-20260803-06 已知限制）。
#
# 注意：backend_guard 的相位閘門只掃 src/kernel/backend/*，**不涵蓋本目錄**。
# 也就是說 src/host/win32/ 的存在不受相位白名單機械保護（見 CHG-20260803-06 §閘門涵蓋缺口）。
if(WIN32)
  add_library(w1_02 STATIC tray_win32.cpp)
  target_include_directories(w1_02 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # 消費 E11-01（TrayBackend / TrayMenu 契約，透遞 E6-01 命令匯流排）。
  target_link_libraries(w1_02 PUBLIC e11_01)
  target_compile_features(w1_02 PUBLIC cxx_std_17)

  add_executable(test_tray_win32 ${CMAKE_SOURCE_DIR}/tests/e11/test_tray_win32.cpp)
  target_link_libraries(test_tray_win32 PRIVATE w1_02 GTest::gtest_main)
  add_test(NAME tray_win32 COMMAND test_tray_win32)
endif()
