# errors.md — 踩過的錯誤

慣例見 [`INDEX.md`](INDEX.md)。每條記「**錯誤 + 根因 + 解法**」，新的在上，機密只記位置不記值。

---

## K-001 — MSVC 以系統 ANSI codepage 誤讀 UTF-8 原始碼，中文字串炸成 69 個編譯錯誤

- 日期：2026-08-03 | 來源：`CHG-20260803-01` | 環境：Windows 11 / MSVC 19.44 / VS Build Tools 2022 17.14

### 錯誤

Windows 首次 MSVC 全建，69 個編譯錯誤：

```
src\kernel\capability_matrix.cpp(20,26): error C2001: 常數中包含新行字元
engine\format\document.cpp(373,9):      error C2143: 語法錯誤: 遺漏 ')' (在 '}' 之前)
```

分佈：C2001 ×27、C2143 ×23、C2146 ×8、C2059 ×4、C2064 ×3、C1075 ×2、C2317 ×1、C2318 ×1。
出錯位置全部指向**含中文的字串字面值**那幾行，程式碼本身看起來完全正常。

### 根因

**不是程式碼問題，是原始碼字集問題。**

本庫原始碼一律 **UTF-8 無 BOM**，註解與字串大量含中文。MSVC 在**沒有 `/utf-8`** 時，
不會假設 UTF-8——它用「非 Unicode 程式的系統地區設定」那個 **ANSI codepage** 去解讀原始碼位元組
（該機為 **CP932**；zh-TW 機器則是 CP950，症狀相同）。

中文字的 UTF-8 位元組序列被誤判為雙位元組字元的**前導碼**，於是把緊接的位元組（包含字串的**結尾引號**）
一起吞掉 → 字串未收尾 → C2001「常數中包含新行字元」。其後的 C2143 / C2146 / C2059 / C1075 等
全是這個的**下游語法崩塌**，不是獨立問題——修掉字集，42 個下游錯誤一起消失。

**為什麼 CI 抓不到**：GCC 與 Clang 預設就以 UTF-8 解讀原始碼。此問題在 Linux / macOS 上
**結構上不可能出現**，而 CI 是 ubuntu + GCC。任何只靠 ubuntu CI 的專案都會在 Windows day-1 首建才撞到。

**為什麼不是「MSVC 比較嚴格」**：與 MSVC 版本、C++ 標準、程式邏輯都無關，
純粹取決於**那台 Windows 機器的系統地區設定**。同一份原始碼在系統地區設定為 UTF-8（beta 選項）的
Windows 上不會出錯——所以它還會**因機器而異**，更難重現。

### 解法

根 `CMakeLists.txt` 加 MSVC 專屬旗標，置於所有 `add_subdirectory()` 之前：

```cmake
if(MSVC)
  add_compile_options(/utf-8)
endif()
```

`/utf-8` 同時把**來源字集**與**執行字集**設為 UTF-8。套用後全建 **0 error、0 warning**，
ctest **176/176 Passed**。

要點：

- **必須用 `if(MSVC)` 包住**。`/utf-8` 是 MSVC 專屬旗標，GCC / Clang 收到會報錯。包住之後
  ubuntu CI 行為與修正前完全相同。
- **位置要夠前面**。`add_compile_options()` 是目錄範圍、只影響**其後**加入的子目錄，
  放在 `add_subdirectory()` 迴圈之後會完全沒效果。
- 不要改用「幫原始碼加 BOM」的解法。那要動 1000 多個檔，且與 `scope_check` 的 write_scope 紀律衝突。
- 別被錯誤數量誤導去逐檔改程式。**69 個錯誤是 1 個根因**；先確認錯誤是否集中在含非 ASCII 字元的行。

### 未關閉的缺口

ubuntu CI **驗證不到這個修正**（GCC 本來就過），故此修正目前只有本機證據。
要讓「Windows 編不編得過」真正進入閘門，必須在 `governance.yml` 加 **windows runner** 跑 MSVC 全建
（`docs/backlog/phase2-desktop-runtime.md` §6 checklist 步驟 3）。**在那之前，這個坑隨時可能以別的形式重開。**
