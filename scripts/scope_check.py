#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CI 閘門：確認本 PR 的檔案變更全部落在該工作單元的鎖定範圍內。

把「不得自行擴權」從紀律變成機器強制。
用法： scope_check.py --unit E1-03 --base origin/main
"""
import argparse, fnmatch, json, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
U = {u["id"]: u for u in json.load(
    open(os.path.join(ROOT, "docs/backlog/units.json"), encoding="utf-8"))["units"]}

# 任何單元都可寫的共用路徑
COMMON = ["docs/worklog/**", "docs/knowledge/errors.md", "docs/changes/**", "docs/acceptance/**"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--unit", required=True)
    ap.add_argument("--base", default="origin/main")
    a = ap.parse_args()
    if a.unit not in U:
        print(f"::error::未知工作單元 {a.unit}（不在 units.json）"); sys.exit(2)

    allow = U[a.unit]["write_scope"] + COMMON
    files = subprocess.run(["git", "diff", "--name-only", f"{a.base}...HEAD"],
                           capture_output=True, text=True, check=True).stdout.split()
    bad = [f for f in files
           if not any(fnmatch.fnmatch(f, p) or fnmatch.fnmatch(f, p.replace("/**", "/*"))
                      for p in allow)]
    if bad:
        print(f"::error::{a.unit} 越權寫入 {len(bad)} 個檔案（鎖定範圍：{allow}）")
        for f in bad:
            print(f"::error file={f}::超出 {a.unit} 的鎖定範圍")
        sys.exit(1)
    print(f"scope_check OK — {len(files)} 個檔案全部落在 {a.unit} 範圍內")


if __name__ == "__main__":
    main()
