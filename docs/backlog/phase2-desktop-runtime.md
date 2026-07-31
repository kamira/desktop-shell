# 桌面 Runtime 路線圖 — 托盤 / 最上層 / 浮動固定 / 點擊穿透 / widget skin

- Date: 2026-07-30 | 角色: I1（協調者）| 狀態: **草案，待決策 A/B**
- 本檔補充 `docs/backlog/PHASE-PLAN.md`（權威相位定義）與 `phase.json`。記錄一個具體需求的完整 backlog：
  把 176 個擴充點單元變成一個 **Rainmeter 式的桌面 runtime**——系統匣圖示 + 選單（widget import、
  最上層切換、浮動⇄固定、點擊穿透）、每 widget 自帶設定檔、skin 套件。

## 0. 需求來源
使用者（2026-07-30）：kernel 若要加 taskbar icon + 選單（widget import、是否最上層、切換浮動/固定使可移動
位置、允許點擊穿透），widget 有自己的設定檔能配置功能與項目顯示位置（本應用能否調整看是否支援），
類似 Rainmeter 的 Galaxy Suite skin——是否需要開新 repo？

## 1. 核心結論
- **引擎/功能不需要新 repo**：所列功能**全部已是現成的擴充點單元**（見 §2 對照）。缺的只是
  「相位 N 的真實後端（cocoa）+ 一個 host shell app 把它們組裝成會跑的 runtime」——都留在 desktop-shell。
- **只有 widget skin 內容包（Galaxy-Suite 那一層）適合獨立 repo**：一個 skin = 一個 E9 套件
  （manifest + E7 宣告式設定檔 + 素材），引擎不改一行就能載入。這正是本架構的目的。

Rainmeter 類比：Rainmeter.exe 本體（引擎+後端+host）= desktop-shell 一個 repo；Galaxy Suite（skin 包）
= 獨立發佈物。

## 2. 功能 → 現有單元對照（皆已合併，扁平化後路徑）

| 需求功能 | 現有單元 | 層 | 檔（扁平後） |
|---|---|---|---|
| 系統匣圖示 + 右鍵選單 | **E11-01** 系統匣圖示與右鍵選單 | platform | `src/host/tray.*` |
| 自繪選單呈現 | E11-02 自繪選單呈現 | platform | `src/host/` |
| 選單「widget import」 | C3-01 啟動器選單樹 + **E6-01** 命令匯流排 + E8/E9 套件載入 | artifact/engine | — |
| 是否最上層（topmost 切換） | **E1-01** 具名圖層與 z-order（Topmost 層） | platform | `src/kernel/named_layers.*` |
| 浮動⇄固定（可移動位置） | **E1-08** 自由拖曳與位置記憶（固定=停用拖曳） | platform | `src/kernel/` |
| 點擊穿透 | **E1-02** 輸入策略四態（ClickThrough / `InputPolicy::PassThrough`） | platform | `src/kernel/input_strategy.*` |
| widget 自帶設定檔 | **E9-01/02** 套件格式+manifest（=skin .ini）、**E7-01** 宣告式 Value | engine | `engine/package/`、`engine/format/` |
| 設定「項目顯示位置」 | E7 宣告式 + **NFR-02 具名 anchor/slot**（`widget.configure(Value)` 已吃） | engine | — |
| 本應用能否調整設定 | **E7-12** 設定值寫回 + **E7-14** 圖形化就地編輯 + E7-15 拖放產生設定項 | engine | `engine/format/` |
| skin 切換 | **E9-04** 主題切換、C3-03 角色外觀組 | engine/artifact | — |

**結論：沒有一項需要新的引擎抽象——全是「組裝現有單元」。**

## 3. 決策點 A / B（相位順序）

`phase.json` 現況：相位1=Mac/null（現在）、相位2=**Windows(win32)**、相位3=跨平台(**cocoa**)。
即**真正的 macOS 桌面後端在原路線的相位 3**。使用者在 Mac、想先看到 Mac 桌面效果，故有兩案：

| 案 | 做法 | 取捨 | phase.json |
|---|---|---|---|
| **A. 提前 cocoa（Mac 優先）** | 相位 2 改為 macOS 期，`allowed_backends: ["null","cocoa"]`；win32 後移 | 最快在開發機看到真桌面 widget；違背「Q1 Windows 優先」的原定案 | `"phase":2` + phases[2] 換 cocoa |
| **B. 照原路線（Windows 優先）** | 先 win32（相位 2）再 cocoa（相位 3） | 守住原定案與「加第三後端不痛」的驗收；Mac 真桌面要等 | `"phase":2` + `["null","win32"]` |

> **注意**：PHASE-PLAN.md 記載「Q1 定案是 Windows 優先」。A 案等於推翻該定案，屬 **medium/需人工核准** 的方向調整，非 I1 自主。**此決策留給使用者。**

以下 backlog 對 A、B 皆適用（差別只在後端是 cocoa 或 win32；host shell / skin 格式共用）。

## 4. Backlog（四個 workstream）

### WS1 — 相位閘門翻轉（infra，前提，先做）
| 項 | 內容 |
|---|---|
| CHG: phase flip | 更新 `phase.json` 當前相位 + 白名單（A→加 cocoa / B→加 win32）；`backend_guard` 自動放行對應 `src/kernel/backend/<name>/`。**Risk: medium**（PHASE-PLAN §相位2 切換程序）|
| CHG: CI 平台矩陣 | **最大卡點**：cocoa 只能在 macOS 建、win32 只能在 Windows 建，現行 CI 是 **ubuntu**。需 governance.yml 加對應 OS runner，或讓平台後端**條件編譯**、非該平台的 CI 跳過該後端目標。**不解決則 G3 必紅。** |
| 契約不變 | E1-25 `tests/contract/` **原封不動**跑新後端——這是相位 1 的投資回收點（PHASE-PLAN 明載）|
| 後續 CHG 清單 | 對每個 `backend_followup: true` 的單元開後續 CHG 補真實後端（見 units.json 該旗標）|

### WS2 — 平台後端單元（實作既有 KernelBackend 契約，不改契約）
契約方法（`src/kernel/backend/null/null_backend.hpp`）：lifecycle(init/shutdown) · capabilities(has) ·
surfaces(create/destroy/show/hide/visible/profile) · frames(begin/end) · input(set_input_policy/poll_input)。
cocoa 對映範例：

| 新單元(暫名) | 實作 | 對映的既有契約/單元 |
|---|---|---|
| P-01 後端核心 | create_surface→NSWindow、show/hide→orderFront/Out、poll_input→NSEvent、frames→繪製 | 過 E1-25 契約測試 |
| P-02 圖層/最上層 | `SurfaceLayer::Topmost`→`NSWindow.level`（.screenSaver/.floating） | E1-01 |
| P-03 輸入策略/穿透 | `InputPolicy::PassThrough`→`ignoresMouseEvents=true` | E1-02 |
| P-04 拖曳 | 真實視窗拖曳 + 落點回報 | E1-08 |
| P-05 系統匣 | `NSStatusItem` + 真右鍵選單 | E11-01 |
| P-06 選單呈現 | `NSMenu` / 自繪 | E11-02 |
| P-07 繪製橋接 | render_model(E4-03/E4-01)→ Core Graphics/Metal | E4 |
（win32 版對映 HWND / SetWindowPos HWND_TOPMOST / WS_EX_TRANSPARENT(穿透) / Shell_NotifyIcon(托盤)。）

### WS3 — Host shell app（把單元串成 runtime）
| 新單元(暫名) | 內容 |
|---|---|
| H-01 shell 主迴圈 | 建後端 → init → 事件迴圈（poll_input→路由→繪製）|
| H-02 托盤選單裝配 | E11-01 托盤 + 選單項（import / 最上層✓ / 浮動⇄固定 / 點擊穿透✓ / 結束），各發 **E6-01 命令** |
| H-03 widget 生命週期 | 載入/卸載/列舉，管每 widget 的 surface |
| H-04 設定持久化 | 開機載入、E7-12 寫回、關機保存（每 widget 位置/開關/最上層態）|
| 位置 | 屬 artifact/app 或新 `host/`；同 repo |

托盤每一項＝發既有命令：最上層→E1-01 改層；浮動固定→E1-08 啟停拖曳；穿透→E1-02 切 PassThrough；import→E9 載入。

### WS4 — widget skin 套件格式（使用者問的設定檔）
| 項 | 內容 |
|---|---|
| SKIN-SPEC | 定義 skin = E9 manifest（requires/permissions）+ E7 宣告式設定檔（選功能 + 具名版位）+ 素材。等同 Rainmeter skin 資料夾 |
| 可調整性 | 設定檔標記可編輯欄位→host 用 E7-14 就地編輯 / E7-12 寫回；未標記則唯讀（「本應用能否調整看是否支援」）|
| 第一個範本 skin | 把 `examples/cpu_gpu_validator` 改寫成獨立 skin 套件，**放獨立 repo**（如 `desktop-shell-skins-starter`）當範本 |

## 5. 規模與風險
- 規模：WS2+WS3 約 **12–15 個新單元**，是一個**完整的新相位**，非收尾。
- 風險①（阻斷）：CI 平台——平台後端無法在 ubuntu 建，WS1 必須先解決 runner/條件編譯。
- 風險②：契約不得分叉——平台碼只能在 `src/kernel/backend/<name>/`，`tests/contract/` 維持平台中立（backend_guard token 檢查）。
- 風險③：A 案推翻「Windows 優先」定案，屬需人工核准的方向決策。

## 6. 決策：**B 案（Windows 優先）已選定**（2026-07-31）

使用者確認「本次交接的目的即為轉移到 Windows 開發」→ 走 **B 案 = 相位 2 win32**，與 PHASE-PLAN 原定案一致（非翻案）。
相位順序維持：相位 2 = win32（Windows），相位 3 = cocoa（Mac 真桌面）。以下 WS 以 win32 為對象。

### Windows 接手 checklist（day-1 順序）
1. **取得原始碼 + 工具鏈**：clone；裝 CMake（≥3.16）+ Visual Studio 2022（MSVC）或 clang-cl。GoogleTest 由 CMake FetchContent 於首次 configure 下載（需網路）。
2. **首次全建（最重要）**：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` → `cmake --build build` → `ctest --test-dir build`。
   - 176 單元平台中立，**預期可編**；但從未在 MSVC 實編過。若有 error/warning，**多為 MSVC 嚴格度**（非邏輯錯）：
     補遺漏的 `#include`、`windows.h` 的 `min/max` 巨集撞 `std::min/max`（加 `NOMINMAX` 或 `#define NOMINMAX`）、
     two-phase lookup、`and/or` 替代 token。逐一機械修正，開 CHG 記錄（infra）。
   - `examples/cpu_gpu_validator`（CHG-20260731-01 已跨平台）走 `#ifdef _WIN32` 的 `GetSystemTimes` 路徑，應可直接編過。
3. **CI 平台矩陣（WS1 的一半）**：現行 governance.yml G3 在 **ubuntu** 建（驗證平台中立性）。加一個 **windows runner** job 跑 MSVC 全建，
   讓「Windows 編不編得過」進入閘門。（或先本機守，CI 之後補。）
4. **翻相位（WS1 的另一半）**：開 CHG（Risk: medium，PHASE-PLAN §相位2 切換程序）→ `phase.json` 設 `"phase": 2`
   （`allowed_backends` 已含 `["null","win32"]`）→ `backend_guard` 自動放行 `src/kernel/backend/win32/`。
5. **開始 WS2**：win32 後端單元（HWND / SetWindowPos HWND_TOPMOST / WS_EX_TRANSPARENT 穿透 / Shell_NotifyIcon 托盤…），
   跑**同一套** E1-25 契約測試（不改一行）。
6. WS2 完成後 → WS3 host shell → WS4 skin 格式。把暫名單元正式編號進 `units.json`，照 autopilot 逐波派工。

### win32 API 對映（WS2 速查）
| 契約 / 需求 | win32 |
|---|---|
| create_surface / show / hide | `CreateWindowEx` / `ShowWindow` |
| SurfaceLayer::Topmost（E1-01） | `SetWindowPos(..., HWND_TOPMOST, ...)` |
| InputPolicy::PassThrough 穿透（E1-02） | `WS_EX_TRANSPARENT | WS_EX_LAYERED` |
| 拖曳（E1-08） | `WM_NCHITTEST` 回 `HTCAPTION` / `SetWindowPos` |
| 系統匣（E11-01） | `Shell_NotifyIcon` + `WM_CONTEXTMENU` |
| poll_input | `PeekMessage` / `GetMessage` 迴圈 |
| 繪製（E4） | GDI+ / Direct2D |
