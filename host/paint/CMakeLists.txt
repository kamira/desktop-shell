# host/paint/ — H1-01（render_model → GDI 繪製橋接）+ H1-06（依 kind 分派）
#
# ---------------------------------------------------------------------------
# 本檔刻意分成兩半，守衛範圍不同（照 W1-08 的先例，CHG-20261111-w1_08）
# ---------------------------------------------------------------------------
# 其一（**不包 if(WIN32)**）：H1-06 的分派核心、組版、NFR-03 降級、以及 E4-18 的接入示範。
#   不碰 GDI、不 include windows.h、無平台條件編譯，因此 **ubuntu 的 G3（-DDS_WERROR=ON）
#   與 gate_sanitizers（ASan + UBSan）都真的建置並執行它們的測試**。
#
#   為什麼非得這樣：H1-06 的驗收標準**是平台承諾本身**——「新增一個元件型別後，不改 src/
#   與 engine/ 任何一行即可被畫出」。那是純邏輯命題，不需要 Windows。若整份照 H1-01 原樣
#   包在 if(WIN32) 裡，ubuntu 的兩道閘門一行都看不到，而 gate_windows 的第二道防線是一份
#   硬編碼名單（backend_win32 / tray_win32 / h1_01_paint / h1_02_controls），不含本單元的
#   測試——連「有沒有被建進來」都沒人查，等於這個單元沒有 CI 證據。
#
# 其二（**整段 if(WIN32)**）：真實 GDI 呼叫（CreateSolidBrush / FillRect / DrawTextW /
#   CreatePen / LineTo），在 ubuntu 編不過，不守衛則 G3 整個 build 紅燈（與 W1-01 / W1-02
#   同理）。其正確性由 gate_windows（windows-latest + MSVC 全建 + ctest）承擔，
#   且 `h1_01_paint` 本來就在該閘門的硬編碼名單內。

# =============================================================================
# 其一：平台中立（全平台建置）
# =============================================================================

# --- H1-06 分派核心：kind → 畫法、PaintSink 抽象、版面幾何純函式 ---
# 刻意**不相依任何 E4 元件 target**：本層只認 ElementDraw（具名字串 kind），
# 元件型別的投影由各自的接入層負責。這也是它能同時被 E4-03 側與 E4-18 側連入的原因
# （見下方 h1_06_radial 關於 ds::elements::RenderModel 同名不同型別的說明）。
add_library(h1_06_dispatch STATIC render_dispatch.cpp)
target_include_directories(h1_06_dispatch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(h1_06_dispatch PUBLIC cxx_std_17)

# --- H1-06 組版 + E4-03 投影：消費 C2-02 的 SystemStatusRenderModel ---
add_library(h1_06_paint_model STATIC widget_render.cpp)
target_include_directories(h1_06_paint_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# 消費 C2-02（SystemStatusRenderModel / MetricRenderEntry）與其透遞的 E4-03 / E4-01 / E7-*，
# 皆上游可讀不可改。標頭直接 include system_status_widget.hpp，故 PUBLIC。
target_link_libraries(h1_06_paint_model PUBLIC h1_06_dispatch c2_02)
target_compile_features(h1_06_paint_model PUBLIC cxx_std_17)

# --- H1-06 E4-18 徑向線 / 指針的接入（「後來才加上的元件型別」實例）---
#
# **為什麼是獨立的 target 與獨立的測試執行檔**：`ds::elements::RenderModel` 有兩個同名
# 不同型別的定義——`modules/elements/bar_gauge.hpp`（E4-03）與
# `modules/elements/radial_pointer.hpp`（E4-18）。同一個 TU 同時 include 兩者會重定義編譯
# 失敗；同一個**程式**連入兩份不同定義則是 ODR 違反（UB，警告旗標與 sanitizer 都抓不到）。
# 故 E4-03 側（h1_06_paint_model / test_h1_06）與 E4-18 側（h1_06_radial /
# test_h1_06_radial）分成兩個程式，各自只有一份定義。
# repo 內 C1-06 / C2-07 的橋接（pimpl）範式處理的是同一族問題（ds::kernel::HitResult）。
add_library(h1_06_radial STATIC radial_draw.cpp)
target_include_directories(h1_06_radial PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(h1_06_radial PUBLIC h1_06_dispatch e4_18)
target_compile_features(h1_06_radial PUBLIC cxx_std_17)

# --- 測試（**註冊在 if(WIN32) 之外**：ubuntu 的 G3 與 gate_sanitizers 都會跑）---
add_executable(test_h1_06 ${CMAKE_SOURCE_DIR}/tests/h1/test_h1_06.cpp)
target_link_libraries(test_h1_06 PRIVATE h1_06_paint_model GTest::gtest_main)
add_test(NAME h1_06_render_dispatch COMMAND test_h1_06)

add_executable(test_h1_06_radial ${CMAKE_SOURCE_DIR}/tests/h1/test_h1_06_radial.cpp)
target_link_libraries(test_h1_06_radial PRIVATE h1_06_radial GTest::gtest_main)
add_test(NAME h1_06_radial_kind COMMAND test_h1_06_radial)

# =============================================================================
# 其二：真實 GDI（僅 Windows）
# =============================================================================
if(WIN32)
  add_library(h1_01_paint STATIC widget_painter.cpp)
  target_include_directories(h1_01_paint PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # H1-06 起：C2-02 經 h1_06_paint_model 透遞（本檔的 GDI 層只實作 PaintSink）。
  target_link_libraries(h1_01_paint PUBLIC h1_06_paint_model)
  target_compile_features(h1_01_paint PUBLIC cxx_std_17)

  add_executable(test_h1_01 ${CMAKE_SOURCE_DIR}/tests/h1/test_h1_01.cpp)
  target_link_libraries(test_h1_01 PRIVATE h1_01_paint GTest::gtest_main)
  add_test(NAME h1_01_paint COMMAND test_h1_01)
endif()
