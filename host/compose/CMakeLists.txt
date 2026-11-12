# host/compose/ — H1-07 組件組合裝配（widget = 一或多個組件）
#
# ---------------------------------------------------------------------------
# 本檔**整份不包 `if(WIN32)`** —— 這是刻意的，理由如下
# ---------------------------------------------------------------------------
# `host/` 既有的每一個子目錄（app / menu / paint / position / tray）都（曾）整份包在
# `if(WIN32)` 裡，因為它們真的呼叫 GDI / shell API。**本單元沒有一行需要 Windows**：
# 消費 E9-03 的組合模型、把組件擺到 widget 內的相對像素位置、依呈現變體切部件框，
# 全部是純邏輯、平台中立的運算。
#
# 為什麼非得放在守衛外（實測數字，H1-06 驗收時由 V1.5 查出、I1 複現）：
# 把一個單元的 `add_executable` / `add_test` 包進 `if(WIN32)` 之後，ubuntu 的註冊測試數
# 從 183 掉回 181、該單元零命中，而**當時三道防線全部放行** ——
#   - G3 的真空綠燈防線只檢查 `> 0`（181 > 0 通過）；
#   - `gate_sanitizers` 的一致性檢查比的是「sanitizer 建置 vs 普通建置」，
#     兩個 ubuntu 建置都排除同一個單元時它們相等（san=181=plain 通過）；
#   - `gate_windows` 的第二道防線是一份**硬編碼名單**
#     （backend_win32 / tray_win32 / h1_01_paint / h1_02_controls），不含本單元。
# 那個洞由 `CHG-20261112-07` 的 **G3b 註冊測試數下界**（PR 不得讓 ubuntu 的註冊數下降）補上，
# 但 G3b 擋的是「數字變少」，它**不會**替一個從一開始就躲在守衛裡的新單元生出覆蓋。
# 所以本單元的 `add_library` / `add_executable` / `add_test` 全部在守衛之外：
# ubuntu 的 **G3（-DDS_WERROR=ON）** 與 **`gate_sanitizers`（ASan + UBSan）** 都真的建置、
# 真的執行它的兩支測試。先例見 `host/paint/CMakeLists.txt`（H1-06：三個中立目標與兩個
# `add_test` 在第 67 行的 `if(WIN32)` **之前**，守衛內只剩 GDI 橋接）與
# `CHG-20261111-w1_08`（政策層抽到守衛外）。
#
# 根 `CMakeLists.txt` 的 `host/*/CMakeLists.txt` GLOB 會自動收入本檔 ——
# **不需要也不得修改根檔**（它不在任何單元的 write_scope 內）。

# =============================================================================
# h1_07_compose — 組合裝配核心（消費 E9-03 / E1-17 / H1-06 的像素矩形）
# =============================================================================
add_library(h1_07_compose STATIC widget_compose.cpp)
target_include_directories(h1_07_compose PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(h1_07_compose PUBLIC cxx_std_17)

# 上游相依（皆已合併於 main，可讀不可改）：
#   - e9_03（E9-03 可互換元件組合）：`Composition` / `ComponentSlot` / `BindResult`
#     —— 插槽宣告 / 綁定 / 替換 / 相容性 / 完整性驗證的唯一來源。並經其 PUBLIC 傳遞
#     E9-01（`Package` / `parse_package`）與 E9-02（`Manifest`）。
#   - e1_17（E1-17 DPI 感知與混合 DPI）：`DpiInfo::scale_of(screen)` —— **具名螢幕的縮放
#     係數（純比值），不是像素**。這是驗收標準那條反向斷言要保住的形狀：
#     kernel 交出比例，像素換算只發生在 host。
#   - h1_06_dispatch（H1-06 繪製層分派核心）：`PaintBox`（半開區間像素矩形）。
#     沿用而不另造第二個像素矩形型別 —— 兩個長得一樣的矩形遲早會有人寫轉換函式、
#     然後在某一側漏掉半開區間的慣例。本層交出的框因此可直接餵進
#     `ElementRendererTable::draw()`。h1_06_dispatch **刻意不相依任何 E4 target**，
#     所以這條相依不會把任何 `ds::elements::*` 型別帶進來（見下方 K-014 說明）。
# 標頭直接 include 三者的標頭，故 PUBLIC 傳遞其 include 目錄。
target_link_libraries(h1_07_compose PUBLIC e9_03 e1_17 h1_06_dispatch)

# =============================================================================
# h1_07_media — 驗收標準指定的第二案例：C2-07 媒體控制作為一個組件
# =============================================================================
add_library(h1_07_media STATIC media_compose.cpp)
target_include_directories(h1_07_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(h1_07_media PUBLIC cxx_std_17)

# c2_07（C2-07 媒體控制 widget）：消費其 `MediaControlRenderModel` /
# `MediaButtonRenderModel`，依呈現變體把它的部件切成 widget 內相對的像素框。
#
# **K-014（同名不同型別的 ODR 陷阱）—— 本目錄刻意踩不到。**
# `ds::elements::RenderModel` 在 `modules/elements/` 下有三個同名不同型別的定義
# （`bar_gauge.hpp:68`（e4_03）/ `radial_pointer.hpp:50`（e4_18）/ `scroll_view.hpp:54`（e4_14））。
# 兩個 TU 各含一份不同定義、連進同一程式 = ODR 違反（UB），而 `-Wall -Wextra -Werror`
# 與 ASan + UBSan **同時零診斷**，唯一現成的機器防線是 `-flto -Wodr`，且僅限該型別跨 TU
# 出現在函式簽章時。
#   - `c2_07` 的 E4 相依是 `e4_02`（`ImageRenderModel`）與 `e4_04`（`ButtonRenderModel`）
#     —— **都不是** `RenderModel` 這個名字，且 `c2_07` 早已在 main 上以單一 target
#     同時連入這兩者、現行建置為綠。
#   - 本目錄的任何 target **都不**連入 `c2_02` / `e4_03` / `e4_18` / `e4_14`，
#     故 `test_h1_07` 這個程式內 `ds::elements::RenderModel` 的定義數為 **0**。
#   - 同 TU 內 `ds::kernel::HitResult` 的碰撞（E1-02 `enum class` vs E1-04 `struct`）由 C2-07
#     自己的橋接層（`media_control_button_bridge`）隔離；本目錄只 include
#     `media_control_widget.hpp`，不直接 include `button_element.hpp`。
target_link_libraries(h1_07_media PUBLIC h1_07_compose c2_07)

# =============================================================================
# h1_07_pixel_audit — 反向斷言的機器化判定（**零相依**）
# =============================================================================
# 「E4 與 kernel 的公開介面不得出現任何像素型別」的判定規則。刻意不相依任何東西
# （連 h1_07_compose 都不）：它要能在**不建置任何被掃描的程式碼**的前提下判定，
# 而且它的三條規則要能被合成字串單獨測試（規則本身可測 = 不會寫成空轉的檢查）。
add_library(h1_07_pixel_audit STATIC pixel_boundary.cpp)
target_include_directories(h1_07_pixel_audit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(h1_07_pixel_audit PUBLIC cxx_std_17)

# =============================================================================
# 測試（**全部註冊在 if(WIN32) 之外**：ubuntu 的 G3 與 gate_sanitizers 都會建、都會跑）
# =============================================================================
add_executable(test_h1_07 ${CMAKE_SOURCE_DIR}/tests/h1/test_h1_07.cpp)
target_link_libraries(test_h1_07 PRIVATE h1_07_media GTest::gtest_main)
add_test(NAME h1_07_compose COMMAND test_h1_07)

add_executable(test_h1_07_pixel_boundary
               ${CMAKE_SOURCE_DIR}/tests/h1/test_h1_07_pixel_boundary.cpp)
target_link_libraries(test_h1_07_pixel_boundary PRIVATE h1_07_pixel_audit GTest::gtest_main)
# 反向斷言要掃的是 **repo 內的真實標頭**，所以測試必須知道 repo 根在哪。
# 以 configure 期的 `${CMAKE_SOURCE_DIR}` 傳入（**不是**開發機的 scratchpad 路徑，
# 也不是任何寫死的絕對路徑）：在 CI 的 ubuntu runner 與任何人的工作目錄上都成立。
# 先例：`src/host/win32/CMakeLists.txt:23` 的 `target_compile_definitions`。
target_compile_definitions(test_h1_07_pixel_boundary
                           PRIVATE DS_H1_07_REPO_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME h1_07_pixel_boundary COMMAND test_h1_07_pixel_boundary)
