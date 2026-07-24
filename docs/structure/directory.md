# 目錄結構

- Date: 2026-07-24 (UTC+0)
- 對應：`docs/architecture.md`（分層與擴充點）、`docs/backlog/units.json`（`write_scope` 為唯一真實來源）

> 本檔是 G4「動了 `src/` 就得同步 `docs/structure/`」的比對對象。
> 目錄若與 `units.json` 的 `write_scope` 不一致，以 `write_scope` 為準並回頭修本檔。

## 現況（由 `units.json` 的 `write_scope` 反推，機器可複算）

```
src/                  對系統的操作（需要 per-platform 後端）—— 換平台時只有這裡要改，共 30 條
├── kernel/           surface kernel、四參數 profile、後端 24 項（含 backend/null 參考實作）
│   └── e1_21/        能力矩陣宣告檔（E1-21）：宣告哪些能力可能在某些平台不存在，
│                     為 has() 能力閘控（NFR-03）與降級路徑的單一資料來源。純宣告 + 查詢介面，
│                     平台中立、不綁後端；相位 1 has() 回傳宣告的預設可用性、未知能力保守回 false。
├── events/           全域事件 3 項：全域熱鍵、系統事件、全域指標手勢（其餘事件已遷 engine/）
└── host/             宿主整合 3 項：系統匣、自繪選單、開機自啟

engine/               平台中立的引擎邏輯（62 項）—— 換平台一行都不用動
├── format/           宣告式格式、變數、公式引擎、熱重載、設定遷移 15 項
├── package/          套件格式、manifest、可互換元件組合、佈局存檔、安裝器 9 項
├── events/           非全域事件 11 項：滑鼠 / 懸停 / 心跳 / 滾輪 / 拖曳判定 / 計時器…
├── render/           繪製基座（paint / transform / clip / text）8 項
├── command/          命令匯流排與分派、動作註冊表、條件動作 5 項
├── script/           腳本引擎、對話直譯器、表現控制、行程內模組載入 5 項
├── ipc/              本機 IPC、低延遲通道、HTTP 端點、獨立行程宿主 5 項
├── metrics/          指標介面契約與採集基礎設施（E2-01 / E2-02）2 項　※真正的感測器在 modules/sysinfo/
└── common/           idle 資源門檻、多語系 2 項

modules/              module 層（58 項）—— 掛在擴充點上的提供者
├── sysinfo/          指標提供者 25 項（CPU / GPU / 記憶體 / 儲存 / IO / 網路 / 電池…）
├── elements/         繪製元件型別 22 項（長條 / 直方圖 / 徑向線 / 折線圖 / 向量圖形…）
└── actuators/        致動器 11 項（音量 / 亮度 / 電源 / 桌布 / 剪貼簿寫入 / 螢幕擷取…）

tests/
├── contract/         跨後端契約測試（不得含平台分支，由 backend_guard 強制）
├── e1/ … e12/        依 units.json 的 `module` 欄位分，**不隨分層搬移**
└── c1/ … c4/         對應 artifact 層

content/              artifact 層：profiles（C1）、widgets（C2）、C3 內容
apps/                 artifact 層：獨立應用型產出物（C4）

docs/                 治理文件（changes / acceptance / structure / knowledge / worklog / backlog）
scripts/              治理工具（plan / scope_check / backend_guard / halt_gate）
```

> `src/` 現在**只剩對系統的操作**：29 項語意分歧（kernel 23 / 全域 events 3 / host 3）
> 加上 `E1-24` null 後端。`E1-24` 雖平台中立，**刻意留在 `src/kernel/backend/`** ——
> 它是後端契約的第一個實作，與 win32/cocoa 同屬後端家族不拆散，
> 且 `backend_guard.py` 以 `src/kernel/backend/*` 執行相位閘門。
> **`E5` 事件子系統橫跨兩處**：3 個全域事件（需 OS 後端）在 `src/events/`，
> 11 個非全域事件（純邏輯）在 `engine/events/` —— 目錄位置即標示是否需要 per-platform 後端。
> `E1-25`（契約測試組，`tests/contract/`）、`E1-26`（後端白名單 lint，`scripts/`）
> 本就不在 `src/`，無需搬移。
> `tests/` 維持依 `module` 欄位（e2 / e3 / e4…）分群 —— `plan.py` 的派工簡報以 `module`
> 組出 `tests/<modl>/test_<slug>*`，改動會使簡報與 `write_scope` 不一致而觸發 G1 紅燈。

## 分層對應

| layer | 位置 | 項數 | 說明 |
|---|---|---|---|
| **platform** | `src/**` 30 條 | 30 | 對系統的操作：29 語意分歧 + `E1-24` null 後端 |
| **engine** | `engine/**` 62 條 + `E1-25`(tests)/`E1-26`(scripts) | 64 | 平台中立邏輯，換平台一行不動 |
| **module** | `modules/sysinfo/**` 25、`modules/elements/**` 22、`modules/actuators/**` 11 | 58 | 掛在擴充點上的提供者 |
| **artifact** | `content/**`、`apps/**`（`C1`–`C4`） | 24 | 驗證器：證明擴充點真的能被外部使用 |

**「感測器不得出現在 `src/`」現在是目錄結構就看得出來的事** ——
若某個指標提供者出現在 `src/`，那就是擴充點沒做好的訊號。

## 目標結構（`docs/architecture.md` 指定）

架構文件要求「`src/` 只放平台，模組放 `modules/`，範例放 `examples/`」，
理由是讓「第三方能不能加」變成目錄結構就看得出來的事。

| 項目 | 狀態 |
|---|---|
| `module` 層（sysinfo / elements / actuators） | **已遷移至 `modules/<subsystem>/`**（CHG-20260723-08） |
| `artifact` 層（C1–C4） | 仍在 `content/`、`apps/`；是否遷移待決 |
| 範例 | `examples/` 尚未建立，含 `examples/hello-module/`（`E8-06` 的受測對象） |

`artifact` 層是否遷移仍待決 —— 需改動 24 個單元的 `write_scope`，屬設計決策。
在此之前 `content/` 與 `apps/` 仍為 `artifact` 層的正式位置。

## 其餘三個結構

`ai-sdlc-autopilot` 的四結構中，本檔為「目錄結構」。
邏輯結構、設計結構、資料結構**尚未產出** —— 待五個擴充點的契約
（`E2-01` / `E4` / `E6-01` / `E1` / `E8-01`+`E8-04`）定稿後補上，
在此之前產出的內容只會是猜測。
