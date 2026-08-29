# worklog — README 補全（`CHG-20260829-01`）

branch/role/scope: `claude/project-analysis-readme-njkw18` / infra 文件變更 /
RW: `README.md`、`docs/images/**`、`docs/changes/**`、`docs/worklog/**`
doing: 分析專案內容並補全 `README.md`，附上實際建置後的執行畫面截圖
last-updated: 2026-08-29 (UTC+0)

## 我要做什麼

1. 讀 `AGENTS.md` → `docs/ai-guideline.md` / `requirements.md` / `architecture.md` /
   `structure/directory.md` / `backlog/*`，取得 README 應有的內容（**一律引用既有文件，不自行發明**）
2. 以 `scripts/plan.py status` 與 `docs/backlog/{units,state,phase}.json` 取得現況數字
3. 實際建置專案並執行 `examples/cpu_gpu_validator`，對畫面取像收錄為 README 的實際畫面
4. 寫 `CHG-20260829-01`，開 PR

## 動工前的狀態確認

| # | 來源 | 結果 |
|---|---|---|
| 1 | 分支 / 遠端基線 | 自最新 `origin/main`（`04b3f15`）重開，clean（K-005） |
| 2 | `docs/knowledge/INDEX.md` | K-001 ~ K-009；本次相關者為 K-001（UTF-8）、K-002（假綠燈） |
| 3 | `docs/changes/` Status | 全數已收尾 |
| 4 | `plan.py status` | 193 單元，已完成 187，進行中 0 |

## 過程中的一個修正

初版把程式輸出重繪成 SVG 當「截圖」。那不成立 —— 數字是真的，但版面與顏色是重繪加上去的。
改為：用專案自己的 CMake 真的建置 → 在 Xvfb 上的真 xterm 執行 → 對 X 畫面取像。
順帶把全庫建起來跑了 `ctest`（176/176 綠），該畫面一併收錄。

## 不做

- 不動任何程式碼（`src/` / `engine/` / `modules/` / `content/` / `apps/` / `host/` / `tests/` / `scripts/`）
- 不動 `units.json` / `state.json`（README 只**讀**現況，不改帳本）
- 不補**原生 Windows** 的實機截圖 —— 本次環境沒有 Windows 機器。
  改以 mingw-w64 交叉編譯 + Wine 虛擬桌面取像（程式碼一行未改），
  README 的圖說明寫「不是原生 Windows 的畫面」，實機驗收仍以 `HANDOFF.md` §0-B 為準

---

## 續：實機 Windows 重拍（`CHG-20260829-02`，2026-08-29）

`CHG-20260829-01` 已合併（`9c5df9c` / #217）。使用者要求把 README 的桌面截圖
改用實機 Windows 重拍，並選定走 repo 自己的 CI（`windows-latest`）而非個人機器。

分支處理：#217 已 squash merge，**不得在已合併的歷史上疊新 commit**，
故自最新 `origin/main` 重開同名分支（`git checkout -B ... origin/main`）。

拆成兩個 PR，因為 `workflow_dispatch` 的 workflow 檔必須先在預設分支上才叫得動：

| PR | 內容 | 狀態 |
|---|---|---|
| A（本輪） | 加 `.github/workflows/screenshot-windows.yml` | 進行中 |
| — | 手動 dispatch，取得 artifact | 待 A 合併 |
| B | 換圖 + 改寫 README 圖說 | 待 |

**還不知道會不會成功**：GitHub 的 windows runner 是無人桌面環境，GUI 能不能被合成出來
要 dispatch 才知道。workflow 內建單色檢查——拍到黑畫面就紅燈，不生出騙人的圖。
