# handshake — autopilot（進場握手 live ack）

branch/role/scope: `docs/chg-20260810-02`（worktree，自 `ef897b8` 重開，避開 K-005）/ I1 / RW: `docs/**`
doing: `CHG-20260810-02` 批次收尾 98 筆 Status 漂移 —— 98 檔已改完、已自驗（未收尾歸 0）
next: commit → push → PR → 閘門綠燈後 squash merge
last-updated: 2026-08-10 08:36 (UTC+0)

## ⚠ hook 的未收尾清單是截斷過的（2026-08-10 發現）

`session_start.py` L76 印的是 **`pending[:5]`**——只有前 5 筆，且按檔名排序。
所以「5 筆未收尾」從來不是總數，是**第一頁**。以 hook 自身的判定邏輯掃 `origin/main`：

- CHG 總數 224、**未收尾 98 筆**（修掉 5 筆前是 103）
- 下一個 session 的 hook 會顯示：`e10_03, e10_04, e10_05, e11_02, e12_01`

**全部 98 筆都只是紀錄漂移，沒有任何一筆是真的缺驗收**——已逐筆查證：

| 分類 | 筆數 | 依據 |
|---|---|---|
| medium 風險 | 13 | **13 筆全部都有對應 ACC**（E1-01/02/03/04/08/09、E1-25、E2-01/02、E4-02、E5-12、E6-01、E7-01） |
| low 風險 | 85 | 依 Guideline §7，low 只需 CHG 內 inline 自驗，本就不需 ACC |
| 非單元 / 查無 | 0 | — |

漂移的三種寫法：沒有 `## Status` 段、`Proposed — self-verified`、
`Proposed — 待 V1 驗收`（32 筆用了這句，但其中 30 筆是 low，根本不需要 V1）。

**已由 `CHG-20260810-02` 批次收尾**：98 筆全部補上 Status（附合併 commit + PR），
重掃結果 `CHG 總數=224 未收尾=0`。fail-closed 略過 0 筆。

**根因未除**：合併發生在 PR 端、CHG 文件在合併前就凍結，仍然沒有任何一步會回頭改
Status。本次只清存量；下一批單元合併後同樣會再長出來。要根治得動
`.github/workflows/` 或 `scripts/`（合併後自動回寫），屬另一張 CHG 的範圍。

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
