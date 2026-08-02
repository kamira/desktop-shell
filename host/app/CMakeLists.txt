# H1-01（其二）host shell app —— 浮在桌面上的 widget
#
# WIN32 子系統（非主控台）：這是桌面浮層，不該連帶開一個主控台視窗。
# 代價是沒有 stdout 可看；診斷改由結束碼與 `--seconds N` 的腳本化驗收承擔。
if(WIN32)
  add_executable(desktop_shell_host WIN32 host_main.cpp)
  target_link_libraries(desktop_shell_host PRIVATE
      w1_01          # W1-01 win32 kernel 後端
      h1_01_paint    # H1-01 GDI 繪製橋接
      c2_02          # C2-02 系統狀態 widget
      e2_01)         # E2-01 指標介面
  target_compile_features(desktop_shell_host PRIVATE cxx_std_17)
endif()
