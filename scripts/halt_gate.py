#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""停點契約閘門：查 halt_policy.json，決定某風險在某停點是 AUTO 還是 HALT。

停點契約是 **tighten-only**：政策沒有明確允許 AUTO 的，一律 HALT。
放寬（把 HALT 改成 AUTO）要改 `assets/halt_policy.json` 並經人工核准；
閘門本身不放行，也不因政策檔缺失而放行。

由 `plan.py gate` 呼叫（governance.yml 的 G6 停點閘門經此鏈路）：
  halt_gate.py --gate <GATE> --risk <RISK>
exit 0 = AUTO（可自動合併）｜exit 10 = HALT（停下等人核准）

用法：
  halt_gate.py --gate before_merge_or_release --risk low      # → AUTO (0)
  halt_gate.py --gate before_merge_or_release --risk medium   # → HALT (10)
"""
import argparse, json, os, sys

# K-001 / K-004 同族：釘住輸出編碼，不依賴主控台/locale 的 ambient 編碼。
# 非 UTF-8 主控台（Windows cp1252 / cp950）印 CJK 會 UnicodeEncodeError，
# 而閘門「當掉」與閘門「擋下」在 exit code 上無法區分（CHG-20260810-05）。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
POLICY_F = os.environ.get("AI_SDLC_HALT_POLICY",
                          os.path.join(ROOT, "assets/halt_policy.json"))

AUTO, HALT = 0, 10


def load_policy():
    if not os.path.exists(POLICY_F):
        # 政策檔缺失 → tighten-only：一律 HALT，不因設定缺席而放行
        sys.stderr.write(f"::warning::halt_policy 不存在（{POLICY_F}），依 tighten-only 一律 HALT\n")
        return None
    return json.load(open(POLICY_F, encoding="utf-8"))


def decide(policy, gate, risk):
    """回傳 AUTO(0) 或 HALT(10)。未知政策 / 未知停點 / 未列入 auto 者一律 HALT。"""
    if policy is None:
        return HALT
    g = policy.get("gates", {}).get(gate)
    if g is None:
        return HALT                      # 未知停點 → HALT
    return AUTO if risk in g.get("auto", []) else HALT


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gate", required=True)
    ap.add_argument("--risk", required=True)
    a = ap.parse_args()
    rc = decide(load_policy(), a.gate, a.risk)
    print(f"halt_gate: gate={a.gate} risk={a.risk} → {'AUTO' if rc == AUTO else 'HALT'}")
    sys.exit(rc)


if __name__ == "__main__":
    main()
