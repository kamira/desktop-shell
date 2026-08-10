# handshake — autopilot（進場握手 live ack）

branch/role/scope: `chore/chg-20260810-03`（worktree，自 `3446ab8` 重開，避開 K-005）/ I1 / RW: `docs/** scripts/** tests/gates/** .github/**`
doing: `CHG-20260810-03` **使用者裁決 B 案**（合併前閘門 G7），已實作 + 自驗（17 測試過）
next: commit → push → PR（本 PR 會是第一個跑 G7 的 PR）→ 綠燈後合併
last-updated: 2026-08-10 08:36 (UTC+0)

## 本輪已完成

| CHG | 內容 | 結果 |
|---|---|---|
| `CHG-20260810-01` | 補齊 hook 標出的 5 筆 Status | 合併於 `ef897b8` (#208) |
| `CHG-20260810-02` | 批次收尾其餘 98 筆 | 合併於 `3446ab8` (#209) |
| `CHG-20260810-03` | 根治：新增 G7 合併前 Status 閘門 | Accepted，待 PR |

`origin/main` 現況：**CHG 總數 225、未收尾 0**（以 hook 自身判定邏輯重掃）。

## 讀取順序與結果（2026-08-10 進場）

| # | 來源 | 結果 |
|---|---|---|
| 1 | 分支 / 工作樹 / 遠端基線 | clean；每次開新分支都自最新 `origin/main` 重開（K-005） |
| 2 | `docs/ai-guideline.md` | v1.1（2026-07-24, Confirmed）；Skill 版本低於執行中的 skill，屬正常（新規則前瞻適用） |
| 3 | `docs/knowledge/INDEX.md` | K-001 ~ K-007。本輪直接踩到 **K-001 / K-004**：PowerShell 讀 CHG 出 mojibake、Python 印 CJK 觸發 `UnicodeEncodeError`（第三次現身，見下） |
| 4 | `docs/coordination.md` | 不存在（本 repo 以 `units.json` + `plan.py` 取代 claim 機制） |
| 5 | `docs/changes/` + `docs/acceptance/` | 已全數收尾（見上表） |
| 6 | `docs/structure/` | 未逐檔比對；本輪未動 `src/`、`engine/`、`modules/` |

## ⚠ hook 的未收尾清單是截斷過的（2026-08-10 發現）

`session_start.py` L76 印的是 **`pending[:5]`**——只有前 5 筆，且按檔名排序。
「未收尾 5 筆」從來不是總數，是**第一頁**。修掉前 5 筆的效果不是清空清單，
是讓下 5 筆遞補上來。實際掃描結果是 **103 筆**。

逐筆查證後確認**無一缺驗收**：13 筆 medium 全部都有對應 ACC；
其餘 low 依 Guideline §7 本就免 ACC；查無合併 commit 者 0 筆。

漂移三種寫法：沒有 `## Status` 段、`Proposed — self-verified`、
`Proposed — 待 V1 驗收`（32 筆用了這句，但其中 30 筆是 low，根本不需要 V1）。

## 根因（`-03` 要處理的）

合併發生在 PR 端，CHG 文件在合併前就凍結在功能分支上，
**閘門只讀 CHG、不寫 CHG**，所以沒有任何一步會回頭改 Status。
`-01`/`-02` 只清了存量；不動流程，下一批單元合併後照樣長出來。

## K-001 / K-004 第三次現身（本輪實錄）

同一個字集陷阱在本輪咬了兩次：PowerShell 讀 UTF-8 的 CHG 印出 mojibake；
複刻 hook 判定邏輯的 Python 腳本印 CJK 直接 `UnicodeEncodeError: 'charmap' codec`。
真正的 `session_start.py` 自己有釘住輸出編碼（L12-14），複刻版漏掉就會**假裝偵測器壞了**。
教訓與 K-001 同：在這台機器上，任何會輸出 CJK 的腳本都要先釘 `encoding="utf-8"`。
