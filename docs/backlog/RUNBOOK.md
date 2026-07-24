# Autopilot 執行手冊

## 前置：Bootstrap（必須人工，只做一次）

**這步不能省。** 自動合併的前提是「CI 綠燈有意義」，而空 repo 的 CI 是**真空綠燈**——
沒有測試就沒有東西會紅。所以在派出任何 subagent 之前，必須先由人工建立：

```bash
git init && git checkout -b main
mkdir -p src tests docs/{changes,acceptance,structure,knowledge,worklog,backlog} scripts .github/workflows
cp <本包>/AGENTS.md .
cp <本包>/docs/ai-guideline.md docs/
cp <本包>/docs/backlog/units.json docs/backlog/
cp <本包>/scripts/{plan.py,scope_check.py} scripts/
cp $AI_SDLC/scripts/halt_gate.py scripts/          # 來自上游 base ai-sdlc skill
cp $AI_SDLC/assets/halt_policy.json assets/
cp <本包>/.github/workflows/governance.yml .github/workflows/
: > docs/knowledge/errors.md
pytest --version && python scripts/plan.py status   # 確認可跑
git add -A && git commit -m "chore: ai-sdlc-autopilot governance bootstrap"
```

GitHub 端設定（一次）：
- 開啟 branch protection on `main`：required status check = `ai-sdlc-autopilot governance / gate`
- 開啟 repo 的 Allow auto-merge
- 建立標籤 `halt:awaiting-human`

## 主迴圈

```bash
# 1) 看下一批可並行派工的單元（相依已滿足且寫入範圍互斥）
python3 scripts/plan.py next --limit 8

# 2) 對每個單元產生派工簡報，交給 subagent
python3 scripts/plan.py brief E1-03 --n 2 > /tmp/brief-E1-03.md

# 3) I1 依簡報派出 I1.n，並稽核其 scoped ack（四把鑰匙必須逐項覆述才准動工）

# 4) subagent 開 PR 後，CI 七道閘門自動跑：
#    G1 鎖定範圍 → G1b 相位閘門 → G2 CHG 連結 → G3 測試 → G4 結構同步 → G5 ACC+身分 → G6 停點契約
#    G6 判 AUTO  → 自動 squash merge + 刪分支
#    G6 判 HALT  → 貼上 halt:awaiting-human 標籤，等你核准

# 5) 完成後回寫狀態
python3 - <<'PY'
import json; s=json.load(open('docs/backlog/state.json'))
s['done'].append('E1-03'); s['in_progress'].pop('E1-03',None)
json.dump(s,open('docs/backlog/state.json','w'),ensure_ascii=False,indent=1)
PY
```

## 並行度

| wave | 單元 | 建議並行 subagent |
|---|---|---|
| 0 | 18 | 8 |
| 1 | 36 | 10 |
| 2 | 36 | 10 |
| 3 | 25 | 8 |
| 4 | 29 | 8 |
| 5 | 17 | 6 |
| 6 | 12 | 5 |
| 7 | 2 | 2 |
| 8 | 1 | 1 |

`plan.py next` 已自動排除**寫入範圍衝突**的單元，同一批派出的 subagent 不會互相踩踏。
agent-hierarchy 建議巢狀深度 2–3 層：你 → I1 → I1.n，不要再往下疊。

## 人工介入點（共 16 次）

16 個 medium 風險單元會在 `before_merge_or_release` 停下。它們是承重 ≥ 8x 的介面設計項
加上 3 個能力閘控項——**正好是錯了最貴的地方**。

想批次核准可在 CHG header 寫
`Autonomy: auto (remaining gates approved by <you> at <gate>, <time UTC+0>)`，
但依契約：**只涵蓋該 CHG，驗收失敗或範圍變動即失效**。

## 為什麼不能全自動

停點契約是 tighten-only 的：放寬要人點頭。這裡有三件事機器驗不出來——

1. **能力閘控項**（`E1-08` `E1-09` `E5-12`）：CI 跑在 Windows 上，
   Wayland 的降級路徑不會被執行到，綠燈不代表跨平台可用。
2. **介面設計品質**：`E2-01`（24x）、`E6-01`（20x）錯的不是行為而是形狀，
   測試會全綠而三個月後全部要改。
3. **真空綠燈**：見 Bootstrap 一節。

這三項不是流程保守，是 CI 能力的邊界。
