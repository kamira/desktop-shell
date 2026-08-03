# H1-01（其二）host shell app —— 浮在桌面上的 widget
#
# WIN32 子系統（非主控台）：這是桌面浮層，不該連帶開一個主控台視窗。
# 代價是沒有 stdout 可看；診斷改由結束碼與 `--seconds N` 的腳本化驗收承擔。
if(WIN32)
  add_executable(desktop_shell_host WIN32 host_main.cpp)
  target_link_libraries(desktop_shell_host PRIVATE
      w1_01          # W1-01 win32 kernel 後端
      w1_02          # W1-02 win32 系統匣後端
      h1_02_controls # H1-02 選單與命令裝配（與 test_h1_02 共用同一份）
      h1_03_position # H1-03 拖曳裝配 + 位置持久化
      h1_04_ui_state # H1-04 UI 開關狀態持久化
      h1_01_paint    # H1-01 GDI 繪製橋接
      c2_02          # C2-02 系統狀態 widget
      e2_01          # E2-01 指標介面
      e11_01         # E11-01 SystemTray / TrayMenu
      e6_01)         # E6-01 命令匯流排
  target_compile_features(desktop_shell_host PRIVATE cxx_std_17)

  # W1-04：把圖示資源複製到執行檔旁邊。
  # 後端解析具名圖示時找的是 `<執行檔目錄>/assets/icons/<name>.ico`——
  # 不複製的話 app 只會拿到程式繪製的後備圖示，而且不會有任何錯誤訊息。
  add_custom_command(TARGET desktop_shell_host POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_SOURCE_DIR}/assets"
              "$<TARGET_FILE_DIR:desktop_shell_host>/assets"
      COMMENT "複製 assets/ 到執行檔目錄（圖示資源）")
endif()
