# handshake — autopilot（進場握手 live ack）

branch/role/scope: `chore/chg-20260810-05`（worktree，自 `5d172a7` 重開，避開 K-005）/ I1 / RW: `docs/** scripts/** tests/** .github/**`
doing: `CHG-20260810-05` 把 tests/e1 接進 CI（新增 G0）+ 五支腳本補釘編碼 —— 已實作 + 自驗（52 測試過）
next: commit → push → PR（本 PR 會是第一個跑 G0 的 PR）→ 閘門綠燈後 infra AUTO 自動合併
last-updated: 2026-08-10 08:36 (UTC+0)

## 本輪已完成

| CHG | 內容 | 結果 |
|---|---|---|
| `CHG-20260810-01` | 補齊 hook 標出的 5 筆 Status | 合併於 `ef897b8` (#208) |
| `CHG-20260810-02` | 批次收尾其餘 98 筆 | 合併於 `3446ab8` (#209) |
| `CHG-20260810-03` | 根治：新增 G7 合併前 Status 閘門 | 合併於 `b6147f0` (#210) |
| `CHG-20260810-04` | 修 G2 命令注入 + G8 workflow_lint | 合併於 `5d172a7` (#211) |
| `CHG-20260810-05` | tests/e1 接進 CI（G0）+ 五支腳本補釘編碼 | Accepted，待 PR |

`origin/main` 現況：未收尾 CHG **0 筆**；CI 閘門 **十一道**（G0 → G8 → G1 → G1b
→ G1c → G2 → G7 → G3 → G4 → G5 → G6）。

## 讀取順序與結果（2026-08-10 進場）

| # | 來源 | 結果 |
|---|---|---|
| 1 | 分支 / 工作樹 / 遠端基線 | clean；每次開新分支都自最新 `origin/main` 重開（K-005） |
| 2 | `docs/ai-guideline.md` | v1.1（2026-07-24, Confirmed）；Skill 版本低於執行中的 skill，屬正常 |
| 3 | `docs/knowledge/INDEX.md` | K-001 ~ **K-008**（本輪新增 K-008） |
| 4 | `docs/coordination.md` | 不存在（本 repo 以 `units.json` + `plan.py` 取代 claim 機制） |
| 5 | `docs/changes/` + `docs/acceptance/` | 已全數收尾 |
| 6 | `docs/structure/` | `directory.md` 已隨 `tests/gates/` 與 `scripts/` 新工具同步 |

## 本輪的三個發現（依嚴重度）

### 1. PR 內文被 CI 當成指令執行（K-008，已修）

`${{ github.event.pull_request.body }}` 直接內插進 G2 的 `run:`。`${{ }}` 是在 shell
看到腳本**之前**展開成字面文字的，所以內文裡的反引號與 `$(...)` 會被 bash 當成命令
替換執行。PR #210 的內文真的被執行了一整排。**那次執行是綠燈**——注入的指令全部
失敗，但 `grep` 成功了，步驟就過了。只看 exit code 永遠看不到它。

已修：G2 及同類 3 處 `base_ref` 全改走 `env:`；新增 G8 `workflow_lint` 以**允許清單**
防復發（黑名單擋不完）。

### 2. hook 的未收尾清單是截斷過的

`session_start.py` L76 印 `pending[:5]`——「未收尾 5 筆」是第一頁，不是總數。
實際 103 筆。逐筆查證後確認**無一缺驗收**：13 筆 medium 全部有 ACC，其餘 low 依
Guideline §7 免 ACC。已由 `-01`/`-02` 清完，`-03` 的 G7 防它再長出來。

### 3. CI 從來沒跑過任何 Python 測試（已由 `-05` 關上）

`tests/e1/test_backend_guard.py`（14 測試，守相位閘門）沒有 `CMakeLists.txt`，
CTest 收不到；`governance.yml` 也沒有 pytest。守閘門的測試自己沒人守。

`-05` 新增 **G0 `run_python_tests.py`**，掃 `tests/` 底下全部 `test_*.py`（52 個測試）。
**沒有用 `unittest discover`**：實測那一行對本 repo 會「跑 0 個測試然後回綠」
（discover 只遞迴進有 `__init__.py` 的套件，`tests/` 的子目錄都不是）——
那正是 G3 已防兩次的真空綠燈。已把這個理由釘成一條測試。

## K-001 / K-004 在本輪出現六次，並長出第三代 K-009

PowerShell 讀 UTF-8 的 CHG 出 mojibake、Python 印 CJK 多次 `UnicodeEncodeError`。
**任何會輸出 CJK 的腳本都要先釘 `sys.stdout.reconfigure(encoding="utf-8")`**——
`scripts/` 七支現在全部釘好了。

**但釘寫入端只解決一半（K-009）**：讀取端的 `subprocess.run(..., text=True)` 不指定
`encoding`，父行程會用 cp1252 解碼子行程的 UTF-8，`UnicodeDecodeError` 在 subprocess
的 **reader thread** 裡拋出——不傳播到呼叫端，只留下 `returncode=0` + `stdout=None`。
看起來像「指令沒有輸出」，不像「解碼失敗」。**看到成功但沒有輸出，先懷疑解碼。**

## 仍未收尾的（非本輪範圍）

`docs/backlog/HANDOFF.md` §0-B：本機防毒封鎖 `desktop_shell_host.exe`，
**6 項操作驗收只有單元測試、沒有實機證據**（自繪選單外觀 / 滑鼠與 Esc 互動 /
彈出位置與邊緣翻轉 / NVDA 朗讀 / 托盤圖示 / 合成拖曳不穩）。轉移到無防毒環境後第一件事。
