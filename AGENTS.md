# AGENTS.md — AI 進入點（任何 agent、任何廠商）

本專案由 **ai-sdlc-autopilot** 治理。工作單元來自機器可讀的 backlog，由 `scripts/plan.py`
自動排程派工，八道 CI 閘門把治理紀律變成機器強制，合併與否由停點契約查表決定。
**任務、鎖定範圍、風險分級、合併權限一律查表得來，沒有憑感覺行事的空間。**

## 1. 進場握手（動任何一行程式碼之前必做）

1. 讀 `docs/ai-guideline.md` —— 需求、範圍、風險分級規則、驗收標準
2. 讀 `docs/knowledge/` INDEX —— 前人踩過的坑，不要重踩
3. 確認開啟中的 CHG 與當前分支狀態
4. 讀 `docs/backlog/PHASE-PLAN.md` —— 當前相位允許哪些平台後端

## 2. 角色與層級

| 角色 | 職責 |
|---|---|
| 人類 | 核准停點、裁決 blocker、批准擴權。唯一能放行 HALT 的角色 |
| **I1**（lead implementer / 協調者） | 派工、追蹤、稽核 subagent 產出、彙整、把錯誤寫進知識庫。**本人不實作任何工作單元** |
| **I1.n**（subagent） | 實作單一工作單元，只寫該單元的 `write_scope` |
| **V1** | medium 風險單元的獨立驗收者，產出 ACC |

agent-hierarchy 深度上限：人類 → I1 → I1.n（2 層），**不得再往下派**。

## 3. 工作從哪裡來

**工作單元來自 `docs/backlog/units.json`，不得自行發明任務。**

| 用途 | 指令 |
|---|---|
| 可派工批次（已排除相依未滿足與寫入範圍衝突） | `python3 scripts/plan.py next [--limit N]` |
| 派工簡報（**原文交給 subagent，不得摘要或改寫**） | `python3 scripts/plan.py brief <UNIT_ID> --n <序號>` |
| 由分支反查工作單元 | `python3 scripts/plan.py unit-for-branch <REF>` |
| 檢查階段相依 | `python3 scripts/stage_check.py [--unit <UNIT_ID>]` |
| 進度總覽 | `python3 scripts/plan.py status` |
| 查停點契約 | `python3 scripts/plan.py gate <UNIT_ID> --gate <GATE>` |

每個 subagent 動工前必須回覆 **scoped ack**，逐項覆述**四把鑰匙**
（分支 / 結構位置 / 需求切片 / 鎖定範圍）。任何一把對不上 → 退回重來，不得放行。

## 4. 不可協商事項

- 變更先過治理：**每個 PR 必須連結 CHG**
- **只寫工作單元的 `write_scope`**；越權由 CI `scope_check` 擋下
- 動工前先在 `docs/worklog/` 寫下「我要做什麼」
- 實作者不自我驗收（medium 以上由 V1 獨立驗收）
- 合併與否由 `scripts/plan.py gate` 查停點契約決定，**agent 不得自行放行**
- 當前相位不允許的平台後端一律不准寫（`backend_guard.py` 會擋）
- 需要超出鎖定範圍 → **停下回報，不得自行擴權**
- 錯誤要記錄「錯誤 + 根因 + 解法」進 `docs/knowledge/errors.md`；機密只記位置不記值

## 5. 十道 CI 閘門（PR 一開就自動跑）

| 閘門 | 擋什麼 |
|---|---|
| **G8** `workflow_lint` | workflow 的 `run:` 直接內插不可信的 `${{ }}`（命令注入，見 knowledge K-008） |
| **G1** `scope_check` | 變更超出該單元的 `write_scope` |
| **G1b** `backend_guard` | 出現當前相位不允許的平台後端 |
| **G1c** `stage_check` | 較早階段依賴較晚階段（NFR-08） |
| **G2** CHG linked | PR 內文缺少 CHG 參照 |
| **G7** `status_check` | 本 PR 牽涉的 CHG 尚未收尾（Status 不是 `Accepted`／`Paused`，或根本沒有 `## Status` 段） |
| **G3** tests | 測試沒綠；**repo 沒有任何測試也算紅**（真空綠燈防線） |
| **G4** structure sync | 動了 `src/` 卻沒更新 `docs/structure/` |
| **G5** ACC + identity | medium 以上缺 ACC，或驗收者與實作者同一人 |
| **G6** halt gate | 停點契約：AUTO → 自動 squash merge；HALT → 貼 `halt:awaiting-human` 等人核准 |

> **執行順序**：G8 → G1 → G1b → G1c → G2 → G7 → G3 → G4 → G5 → G6。
> G8 最先：它守的是這個 workflow 自己，本檔若已可被注入，後面每一道閘門的判定都不再可信。
> G7 排在耗時的 G3 建置之前（純文字檢查，紅燈時省下十餘分鐘）。
> 刻意擱置的 WIP 寫 `Paused — <理由>` 即可通過 G7——擋它只會逼人把 Proposed 謊報成 Accepted。
>
> **寫 workflow 的硬規則**：PR 內文／標題／分支名一律經 `env:` 傳進 `run:`，
> 不得直接 `${{ }}` 內插。`${{ }}` 是在 shell 看到腳本之前展開成字面文字的，
> 內插等於把使用者輸入貼進腳本——2026-08-10 真的被執行過（K-008）。

## 6. 治理文件位置

`docs/` 之下：`changes/`（CHG）、`acceptance/`（ACC）、`structure/`、`knowledge/`、
`worklog/`、`backlog/`。
