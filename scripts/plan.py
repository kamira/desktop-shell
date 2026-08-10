#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ai-sdlc-autopilot 排程器：讀 units.json，算出可派工批次，產生 subagent 派工簡報。

用法：
  plan.py status                 # 各 wave 進度
  plan.py next [--limit N]       # 下一批可並行派工的單元
  plan.py brief <UNIT_ID>        # 產生該單元的 subagent 派工簡報（scoped handshake）
  plan.py gate <UNIT_ID> --gate <GATE>   # 查停點契約，exit 0=AUTO 10=HALT
  plan.py unit-for-branch <REF>  # 由分支名 feat/<slug> 反查單元，輸出 id/risk/phase（供 CI $GITHUB_OUTPUT）
"""
import argparse, json, os, subprocess, sys, collections

# K-001 / K-004 同族：釘住輸出編碼，不依賴主控台/locale 的 ambient 編碼。
# 非 UTF-8 主控台（Windows cp1252 / cp950）印 CJK 會 UnicodeEncodeError，
# 而閘門「當掉」與閘門「擋下」在 exit code 上無法區分（CHG-20260810-05）。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M = json.load(open(os.path.join(ROOT, "docs/backlog/units.json"), encoding="utf-8"))
U = {u["id"]: u for u in M["units"]}
STATE_F = os.path.join(ROOT, "docs/backlog/state.json")
HALT = os.environ.get("AI_SDLC_HALT_GATE", "scripts/halt_gate.py")


def state():
    if os.path.exists(STATE_F):
        return json.load(open(STATE_F, encoding="utf-8"))
    return {"done": [], "in_progress": {}}


def ready(st, limit=None):
    """相依已完成、且寫入範圍與進行中的單元互斥者，才可派工。"""
    done, busy = set(st["done"]), set(st["in_progress"])
    taken = {s for i in busy for s in U[i]["write_scope"]}
    out = []
    for u in sorted(M["units"], key=lambda x: (x["wave"], -x["load_bearing"], x["id"])):
        if u["id"] in done or u["id"] in busy:
            continue
        if not set(u["depends_on"]) <= done:
            continue
        if set(u["write_scope"]) & taken:          # 範圍衝突 → 這輪不派
            continue
        taken |= set(u["write_scope"])
        out.append(u)
        if limit and len(out) >= limit:
            break
    return out


BRIEF = """# 派工簡報 — {id}

你是 subagent `I1.{n}`。**只做本簡報指定的事，只寫允許範圍內的檔案。**
開工前必須先回覆 scoped ack（見文末四把鑰匙），未回覆不得動工。

## 四把鑰匙（ack 時逐項覆述）
1. 分支：`{branch}`（從 `main` 開，不得在其他分支作業）
2. 結構位置：`{scope0}`
3. 需求切片：{title}（{id}，優先級 {priority}，功能群 {group}）
4. 鎖定範圍（**唯一可寫入的路徑**）：
{scope_list}

## 上游相依（已完成，可讀不可改）
{deps}

## K 原語
{prims}

## 風險與自主性
- Risk: **{risk}**（承重 {lb}x）
- 停點：`before_merge_or_release` 依契約查表；{autonomy_note}

## 必須產出
1. 實作程式碼（僅限鎖定範圍）
2. `tests/{modl}/test_{slug}*` — 至少一個可重跑的測試
3. `docs/changes/CHG-<YYYYMMDD>-{slug}.md` — CHG 記錄，Header 必填
   `Implemented by: I1.{n}`、`Risk: {risk}`、`Branch: {branch}`
4. `docs/worklog/I1.{n}-{slug}.md` — **動工前先寫「我要做什麼」**，遇錯記錄
   「錯誤 + 根因 + 解法」，機密只記位置不記值

## 驗收標準
{acceptance}

## 禁止事項
- 寫入鎖定範圍以外的任何路徑（CI 的 scope_check 會擋，PR 直接紅燈）
- 自行擴權；需要超出範圍 → 停下回報 I1，不得自行決定
- 自我驗收（{selfcheck}）
- 靜默吞錯

## 完成後回報
輸出清單 + 本次遇到的錯誤列表（沒有就明說）+ CHG 路徑 + PR 連結
"""


def brief(uid, n=1):
    u = U[uid]
    slug = uid.lower().replace("-", "_")
    dep = "\n".join(f"- `{d}` {U[d]['title']}" for d in u["depends_on"]) or "- （無）"
    aut = ("**本單元 `Autonomy: halt`** — 能力閘控項，CI 在 Windows 上驗不出跨平台降級路徑，"
           "合併前一律停下等人。" if u["autonomy"] == "halt"
           else "low 風險依契約自主續跑" if u["risk"] == "low"
           else "medium 風險 → 合併前停下等人")
    sc = "\n".join(f"   - `{s}`" for s in u["write_scope"])
    return BRIEF.format(
        id=uid, n=n, branch=u["branch"], scope0=u["write_scope"][0],
        title=u["title"], priority=u["priority"], group=u["group"],
        scope_list=sc, deps=dep,
        prims="、".join(u["primitives"]) or "—",
        risk=u["risk"], lb=u["load_bearing"], autonomy_note=aut,
        modl=u["module"].lower(), slug=slug, acceptance=u["acceptance"],
        selfcheck="low 風險可 inline 自驗，但仍須留可重跑證據"
                  if u["risk"] == "low" else "medium 風險必須由 V1 獨立驗收")


def unit_for_branch(ref):
    """由分支名反查單元。接受 feat/<slug> 或 refs/heads/feat/<slug>；
    slug 與 id 的對應：id.lower().replace('-','_')。找不到→exit 1（CI 步驟即紅燈）。"""
    slug = ref.rsplit("/", 1)[-1] if ref else ""
    u = next((x for x in M["units"] if x["id"].lower().replace("-", "_") == slug), None)
    if u is None:
        sys.stderr.write(f"::error::分支名 {ref} 對不到任何工作單元\n")
        sys.exit(1)
    return u


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    n = sub.add_parser("next"); n.add_argument("--limit", type=int, default=8)
    b = sub.add_parser("brief"); b.add_argument("unit"); b.add_argument("--n", type=int, default=1)
    g = sub.add_parser("gate"); g.add_argument("unit"); g.add_argument("--gate", required=True)
    ub = sub.add_parser("unit-for-branch"); ub.add_argument("ref")
    a = p.parse_args()

    if a.cmd == "unit-for-branch":
        u = unit_for_branch(a.ref)
        print(f"id={u['id']}")
        print(f"risk={u['risk']}")
        print(f"phase={u['phase']}")
        return

    st = state()

    if a.cmd == "status":
        done = set(st["done"])
        w = collections.defaultdict(lambda: [0, 0])
        for u in M["units"]:
            w[u["wave"]][1] += 1
            if u["id"] in done: w[u["wave"]][0] += 1
        print(f"總計 {len(M['units'])} 單元｜已完成 {len(done)}｜進行中 {len(st['in_progress'])}")
        for k in sorted(w):
            d, t = w[k]
            print(f"  wave {k}: {d}/{t} {'█'*int(20*d/t)}{'░'*(20-int(20*d/t))}")
        return

    if a.cmd == "next":
        r = ready(st, a.limit)
        if not r:
            print("沒有可派工單元（相依未滿足或範圍衝突）"); return
        print(f"可並行派工 {len(r)} 項：")
        for i, u in enumerate(r, 1):
            fl = " ⚠HALT" if u["autonomy"] == "halt" else ""
            print(f"  I1.{i}  {u['id']:<7} {u['title'][:34]:<36} "
                  f"[{u['risk']}] w{u['wave']} {u['load_bearing']}x{fl}")
        return

    if a.cmd == "brief":
        print(brief(a.unit, a.n)); return

    if a.cmd == "gate":
        u = U[a.unit]
        if u["autonomy"] == "halt":
            print("HALT (unit override)"); sys.exit(10)
        rc = subprocess.run([sys.executable, HALT, "--gate", a.gate, "--risk", u["risk"]])
        sys.exit(rc.returncode)


if __name__ == "__main__":
    main()
