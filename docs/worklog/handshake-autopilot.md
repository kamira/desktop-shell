# handshake — autopilot（進場握手 live ack）

branch/role/scope: `claude/ai-sdlc-autopilot-handshake-47f9f2`（worktree）/ I1 lead implementer / RW: 尚未鎖定任何 `write_scope`
doing: `CHG-20260810-01`（docs-only）補齊 5 筆已合併 CHG 的 Status —— 5 檔已改完、已自驗
next: commit → push → PR（內文帶 CHG-20260810-01）→ 閘門綠燈後 squash merge
last-updated: 2026-08-10 08:36 (UTC+0)

## 讀取順序與結果

| # | 來源 | 結果 |
|---|---|---|
| 1 | 分支 / 工作樹 / 遠端基線 | worktree clean；`origin/main...HEAD` = 0 behind / 0 ahead（已 `git fetch`） |
| 2 | `docs/ai-guideline.md` | v1.1（2026-07-24, Confirmed）；Skill 記為 ai-sdlc-autopilot v1.1（基於 ai-sdlc v1.17）——**低於執行中的 skill 版本，屬正常**（新規則前瞻適用） |
| 3 | `docs/knowledge/INDEX.md` | K-001 ~ K-007（7 條）。本次環境直接相關：**K-001 / K-004**（UTF-8 被 ANSI codepage 誤讀）——本次以 PowerShell 讀 CHG 即重現 mojibake，已改用 Read 工具 |
| 4 | `docs/coordination.md` | **不存在**（本 repo 以 `docs/backlog/units.json` + `plan.py` 取代 claim 機制） |
| 5 | `docs/changes/` + `docs/acceptance/` | 223 CHG / 16 ACC；hook 標記 5 筆未收尾 → **經查為 Status 欄陳舊，非未完成工作**（詳下） |
| 6 | `docs/structure/` | 未逐檔比對（本次為 handshake only，未動程式碼） |

## hook 標記的 5 筆「未收尾 CHG」— 查證結論

5 筆的 `## Status` 都仍寫著「Proposed — self-verified（low risk）。未 push、未開 PR。」，
但 `origin/main` 上五個單元**都已經 squash merge**：

| CHG | 合併 commit | PR |
|---|---|---|
| `CHG-20260724-c1_07` | `85bc478` 全螢幕調光層 profile | #157 |
| `CHG-20260724-c3_01` | `0e46f6c` 啟動器選單樹 | #172 |
| `CHG-20260724-c3_03` | `cbd5611` 角色外觀組 | #184 |
| `CHG-20260724-e10_01` | `9f983f2` 本機 IPC 訊息投遞 | #24 |
| `CHG-20260724-e10_02` | `ac7ff4f` 低延遲同機通道 | #83 |

**根因**：合併發生在 PR 端，CHG 文件在合併前就凍結在分支上，流程沒有任何一步會回頭改
Status。e10_01 / e10_02 停在 `Proposed`；c1_07 / c3_01 / c3_03 **根本沒有 Status 段**。
是紀錄漂移（doc-integrity），不是待辦工作。
**已修**：`CHG-20260810-01`（docs-only, low）補齊 5 筆 Status，各自附合併 commit + PR 編號。

## 真正未收尾的東西

`docs/backlog/HANDOFF.md` §0-B：本機防毒（PC-cillin）封鎖 `desktop_shell_host.exe` 執行
（`Access is denied`，未繞過），導致 **6 項操作驗收只有單元測試、沒有實機證據**：
自繪選單外觀 / 選單滑鼠與 Esc 互動 / 選單彈出位置與邊緣翻轉 / NVDA·Narrator 實際朗讀 /
托盤圖示外觀 / 合成拖曳時好時壞的成因（W1-03~W1-06、H1-04）。
`CHG-20260803-15` 的 Status 明寫「操作驗收未完成」。轉移到無防毒環境後這是第一件事。
