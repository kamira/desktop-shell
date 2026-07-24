# 給 Claude Code 的指令

以下四段直接複製貼上。**不要改寫措辭**——「四把鑰匙」「scoped ack」
這些是 ai-sdlc-autopilot 契約裡的固定詞，改了 subagent 就對不上。

---

## 指令 1 — 進場握手（第一次開 session 必跑）

```
讀 AGENTS.md，依其指示完成 ai-sdlc-autopilot 進場握手。

你的角色是 I1（lead implementer / 協調者）。你的職責是派工、追蹤、
稽核 subagent 產出、彙整、把錯誤寫進知識庫。你本人不實作任何工作單元。

進場握手完成後，做以下驗證並回報，不要開始實作：

1. 確認 bootstrap 完整：
   - scripts/{plan.py,scope_check.py,backend_guard.py,halt_gate.py} 皆可執行
   - docs/backlog/{units.json,phase.json,state.json} 存在
   - .github/workflows/governance.yml 存在
   - assets/halt_policy.json 存在
2. 跑 `python3 scripts/plan.py status`，回報進度
3. 跑 `python3 scripts/backend_guard.py`，確認相位閘門通過
4. 讀 docs/backlog/PHASE-PLAN.md，用三句話回報當前相位的限制
5. 跑 `python3 scripts/plan.py next --limit 8`，回報可派工批次

回報格式：逐項 ✅/❌ + 下一步建議。有任何 ❌ 就停下等我，不要自己修。
```

---

## 指令 2 — 派工一批

```
依 docs/backlog/units.json 派工。規則：

1. 跑 `python3 scripts/plan.py next --limit N` 取得可並行批次
   （N 建議 4–6；plan.py 已排除寫入範圍衝突，同批不會互踩）
2. 對每個單元跑 `python3 scripts/plan.py brief <UNIT_ID> --n <序號>`
   取得派工簡報，**原文交給 subagent，不要摘要或改寫**
3. 每個 subagent 回覆 scoped ack（逐項覆述四把鑰匙）之後才准動工。
   ack 有任何一把鑰匙對不上 → 退回重來，不要放行
4. subagent 完成後開 PR，CI 七道閘門自動跑
5. 你負責稽核：確認產出在鎖定範圍內、CHG header 完整、
   worklog 有動工前的紀錄、錯誤有記錄根因與解法
6. 把所有 subagent 回報的錯誤彙整進 docs/knowledge/errors.md（去重、歸納通則）

限制：
- 你不得自行發明工作單元。不在 units.json 裡的事情一律先問我
- 你不得放行停點。G6 判 HALT 就是等我，不要想辦法繞過
- subagent 不得再往下派（agent-hierarchy 深度上限 2–3 層，你→I1.n 已是 2 層）
- 相位 1 禁止任何真實平台後端。想寫 backend/cocoa 或 backend/win32 → 停下問我

開始前先把你打算派的批次列給我看，我點頭再派。
```

---

## 指令 3 — 收斂

```
本批 subagent 全部完成後：

1. 逐一確認 PR 狀態。已 merge 的把單元 ID 加入 docs/backlog/state.json 的 done，
   並從 in_progress 移除
2. 被貼上 halt:awaiting-human 標籤的列給我，附上該單元的 risk 與承重度，
   以及一句話說明「我該看什麼」——不要只說「等你核准」
3. 跑 `python3 scripts/plan.py status` 回報進度
4. 檢查 docs/knowledge/errors.md：本批有沒有重複出現的錯誤模式？
   有的話歸納成通則寫進去，並說明下批派工要怎麼避開
5. 若有單元卡住（subagent 回報 blocker），把 blocker 與已嘗試方案列給我
```

---

## 指令 4 — 相位切換（相位 1 完成後才用）

```
準備從相位 1（Mac / null 後端）切換到相位 2（Windows / win32 後端）。

1. 確認相位 1 的 147 個單元全數 Accepted，特別是 E1-21 / E1-25 / E1-24
   （能力矩陣、契約測試組、null 後端）
2. 確認 tests/contract/ 沒有任何平台分支
3. 開一份 CHG 記錄相位切換決策，Risk: medium
4. 更新 docs/backlog/phase.json 為
   {"phase":2,"allowed_backends":["null","win32"]}
5. 對每個 backend_followup: true 的單元（共 63 個）開後續 CHG

重點：契約測試一行都不准改。它們在相位 1 已被 null 後端驗證過，
現在拿同一組去跑 win32 後端——那才是相位 1 的投資回收點。
若發現契約測試需要為 win32 修改，那就是相位 1 的介面設計有問題，
停下來報告，不要改測試遷就實作。
```
