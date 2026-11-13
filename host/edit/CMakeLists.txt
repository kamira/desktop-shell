# host/edit/ — H1-09 就地編輯裝配
#
# ---------------------------------------------------------------------------
# 本檔**整份不包 `if(WIN32)`** —— 與 `host/compose/`（H1-07）同樣是刻意的
# ---------------------------------------------------------------------------
# H1-09 的 `units.json` 標 **`platform: neutral`**，而本單元的內容（編輯設定值、序列化、
# 版本分流、幾何消毒、kind 分派）沒有一行需要 Windows。
#
# 根 `CMakeLists.txt` 第 91 行那句「host/ 各子目錄自帶 `if(WIN32)` 守衛」**已經過時**：
# `host/compose/` 整份沒有守衛，`host/paint/` 只有 GDI 那一半有。本檔沿用那個較新的先例。
# （根檔不在任何單元的 write_scope 內，故**不改它**；它的 `host/*/CMakeLists.txt` GLOB
#  會自動收入本檔。）
#
# 為什麼非得放在守衛外（實測數字，H1-06 驗收時由 V1.5 查出、I1 複現，載於
# `host/compose/CMakeLists.txt` 與 `CHG-20261112-07`）：把一個單元的 `add_executable` /
# `add_test` 包進 `if(WIN32)`，ubuntu 的註冊測試數會掉、該單元零命中，而三道防線當時全部
# 放行（G3 的真空綠燈防線只檢查 `> 0`；`gate_sanitizers` 比的是兩個 ubuntu 建置，同時
# 排除同一個單元時它們相等；`gate_windows` 的第二道防線是一份硬編碼名單，不含新單元）。
# 該洞由 G3b（註冊數不得下降）與 **G3c**（`platform: neutral` 的單元，ubuntu 的 `ctest -N`
# 註冊表裡必須有一支名稱含該單元 id 的測試）補上 —— 下面兩個 `add_test` 的名稱都含
# `h1_09`，且都在守衛之外，G3c 因此查得到。

# =============================================================================
# h1_09_edit — 就地編輯裝配核心
# =============================================================================
add_library(h1_09_edit STATIC edit_assembly.cpp)
target_include_directories(h1_09_edit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(h1_09_edit PUBLIC cxx_std_17)

# 上游相依（皆已合併於 main，可讀不可改）。標頭直接 include 全部六者，故一律 PUBLIC。
#   - e9_05（E9-05 佈局存檔與還原）：`LayoutState` / `LayoutStorage` / `LayoutStore` /
#     `serialize_layout` / `kLayoutFormat` —— **存檔格式的唯一事實來源**，本層不另造。
#     並經其 PUBLIC 傳遞 e7_12 → e7_01。
#   - e7_14（E7-14 圖形化設定與就地編輯）：`apply_edit` —— 編輯前的型別 / 約束驗證。
#   - e7_15（E7-15 拖放產生設定項）：`DropContent` / `generate_config_item`。
#     （其 PUBLIC 相依 e5_08 系統事件一併傳遞；e5_08 為 engine 層、平台中立。）
#   - e7_08（E7-08 設定遷移）：`MigrationRegistry` / `version_less` —— 舊版存檔的升級鏈。
#   - e7_07（E7-07 熱重載）：`ReloadSource` / `HotReloader`（經其傳遞 e7_06 的診斷）。
#   - h1_06_dispatch（H1-06 繪製層分派核心）：`ElementDraw` / `PaintBox` /
#     `ElementRendererTable` —— 換 kind 之後造型真的會變，靠的就是這張分派表。
#
# **K-014（同名不同型別的 ODR 陷阱）—— 本目錄刻意踩不到。**
# `ds::elements::RenderModel` 在 `modules/elements/` 下有三個同名但結構完全不同的定義
# （`bar_gauge.hpp`（E4-03）/ `radial_pointer.hpp`（E4-18）/ `scroll_view.hpp`（E4-14））。
# 兩份不同定義連進同一個程式 = ODR 違反（UB），而 `-Wall -Wextra -Werror` 與
# ASan + UBSan **同時零診斷**。本目錄的任何 target **都不**連入任何 `e4_*` / `c2_*`：
# `h1_06_dispatch` 自己就刻意不相依任何 E4 target（它只認具名字串 kind），
# 所以 `test_h1_09*` 這兩個程式內 `ds::elements::RenderModel` 的定義數為 **0**。
target_link_libraries(h1_09_edit PUBLIC e9_05 e7_14 e7_15 e7_08 e7_07 h1_06_dispatch)

# =============================================================================
# 測試（**全部註冊在 if(WIN32) 之外**：ubuntu 的 G3 / G3b / G3c 與 gate_sanitizers 都會跑）
# =============================================================================

# 驗收標準的兩條端到端情境 + 與 E9-05 的格式一致性。
add_executable(test_h1_09 ${CMAKE_SOURCE_DIR}/tests/h1/test_h1_09.cpp)
target_link_libraries(test_h1_09 PRIVATE h1_09_edit GTest::gtest_main)
add_test(NAME h1_09_edit_assembly COMMAND test_h1_09)

# 「使用者可直接改動持久化狀態，故錯誤不得讓佈局進入救不回來的狀態」那一條。
add_executable(test_h1_09_recovery ${CMAKE_SOURCE_DIR}/tests/h1/test_h1_09_recovery.cpp)
target_link_libraries(test_h1_09_recovery PRIVATE h1_09_edit GTest::gtest_main)
add_test(NAME h1_09_persisted_state_recovery COMMAND test_h1_09_recovery)
