# W1-08 win32 行程列舉後端
#
# 本單元首次建立 `src/sysinfo/` —— sysinfo 平台後端的家。根 CMakeLists 的 GLOB 已含
# `src/*/*/CMakeLists.txt`，自動收得到本檔（根檔不在本單元 write_scope 內，一行未動）。
#
# ---------------------------------------------------------------------------
# 本檔刻意分成兩半，守衛範圍不同
# ---------------------------------------------------------------------------
# 其一 `w1_08_policy`（**不包 if(WIN32)**）：原始讀數 → ProcessSample 的換算與
#   「什麼情況算無讀值」的政策，不碰 win32 API、不含平台條件編譯。因此 ubuntu 的 G3
#   與開發機都真的建置並執行它的測試 —— 本單元最容易出錯的部分（K-002 的邊界、差分式
#   CPU% 的除數、pid 重用）由此進入閘門守備範圍。照 H1-01 的先例
#   （CHG-20260803-05-h1_01：版面計算抽成不碰 GDI 的純函式）。
#
# 其二 `w1_08`（**整段 if(WIN32)**）：真實 win32 呼叫（EnumProcesses / OpenProcess /
#   QueryFullProcessImageNameW / GetProcessTimes / GetProcessMemoryInfo），在 ubuntu /
#   macOS 編不過，不守衛則 G3 整個 build 紅燈（與 W1-01 / W1-02 / H1-01 同理）。
#
# 代價的**現況**（與 W1-01 檔頭那句寫於 windows runner 上線前的話不同）：
#   `if(WIN32)` 讓 ubuntu G3 看不到其二，但 **`gate_windows`（windows-latest + MSVC 全建
#   + ctest）已經存在**，故其二的正確性有 CI 證據，不再只有開發機的手動證據。
#   唯一殘留的缺口：`gate_windows` 第二步「確認 Windows 專屬單元真的有被建進來」是一份
#   **硬編碼名單**（backend_win32 / tray_win32 / h1_01_paint / h1_02_controls），
#   本單元的 `process_win32` 不在其中。補名單需改 `.github/workflows/governance.yml`，
#   **不在本單元的 write_scope 內** —— 已在 CHG-20261111-w1_08 §已知限制交回 I1。

# --- 其一：平台中立的政策層（全平台建置） ---
add_library(w1_08_policy STATIC process_policy.cpp)
target_include_directories(w1_08_policy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# 消費 E2-19（ProcessSample / ProcessInfo / ProcessStatus / ProcessSource 契約），上游可讀不可改。
# PUBLIC：本層對外標頭 include 了 process_monitor.hpp，須把上游的 include 目錄向下游傳遞。
target_link_libraries(w1_08_policy PUBLIC e2_19)
target_compile_features(w1_08_policy PUBLIC cxx_std_17)

# 政策層測試在 ubuntu 與 Windows 都跑（本單元的可重跑證據主體）。
add_executable(test_process_win32_policy
    ${CMAKE_SOURCE_DIR}/tests/e2/test_process_win32_policy.cpp)
target_link_libraries(test_process_win32_policy PRIVATE w1_08_policy GTest::gtest_main)
add_test(NAME w1_08_policy COMMAND test_process_win32_policy)

# --- 其二：真實 win32 列舉（僅 Windows） ---
if(WIN32)
  add_library(w1_08 STATIC process_source_win32.cpp)
  target_include_directories(w1_08 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # psapi：EnumProcesses / GetProcessMemoryInfo。
  target_link_libraries(w1_08 PUBLIC w1_08_policy psapi)
  target_compile_features(w1_08 PUBLIC cxx_std_17)

  add_executable(test_process_win32 ${CMAKE_SOURCE_DIR}/tests/e2/test_process_win32.cpp)
  target_link_libraries(test_process_win32 PRIVATE w1_08 GTest::gtest_main)
  add_test(NAME process_win32 COMMAND test_process_win32)
endif()
