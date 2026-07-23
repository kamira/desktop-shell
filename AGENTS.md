# AGENTS.md — AI 進入點（任何 agent、任何廠商）

1. **動任何一行程式碼之前必做**：跑 ai-sdlc 進場握手 —
   `docs/ai-guideline.md` → `docs/knowledge/` INDEX → 開啟中的 CHG / 分支狀態。
2. 治理文件在 `docs/`（`changes/` `acceptance/` `structure/` `knowledge/` `worklog/` `backlog/`）。
3. **工作單元來自 `docs/backlog/units.json`，不得自行發明任務。**
   取得可派工批次：`python3 scripts/plan.py next`
   取得派工簡報：`python3 scripts/plan.py brief <UNIT_ID>`
4. 不可協商事項：
   - 變更先過治理（每個 PR 必須連結 CHG）
   - **只寫工作單元的 `write_scope`；越權由 CI `scope_check` 擋下**
   - 動工前先在 `docs/worklog/` 寫下「我要做什麼」
   - 實作者不自我驗收（medium 以上由 V1 獨立驗收）
   - 合併與否由 `scripts/plan.py gate` 查停點契約決定，agent 不得自行放行
