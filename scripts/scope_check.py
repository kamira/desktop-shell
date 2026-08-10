#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CI 閘門：確認本 PR 的檔案變更全部落在該工作單元的鎖定範圍內。

把「不得自行擴權」從紀律變成機器強制。
用法： scope_check.py --unit E1-03 --base origin/main
"""
import argparse, fnmatch, json, os, subprocess, sys

# K-001 / K-004 同族：釘住輸出編碼，不依賴主控台/locale 的 ambient 編碼。
# 非 UTF-8 主控台（Windows cp1252 / cp950）印 CJK 會 UnicodeEncodeError，
# 而閘門「當掉」與閘門「擋下」在 exit code 上無法區分（CHG-20260810-05）。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
U = {u["id"]: u for u in json.load(
    open(os.path.join(ROOT, "docs/backlog/units.json"), encoding="utf-8"))["units"]}

# 任何單元都可寫的共用路徑
# docs/structure/** 必須放行：G4 要求「動了 src/ 就得同步 docs/structure/」，
# 若不放行，寫 src/ 的單元會同時被 G1（越權）與 G4（未同步）夾殺，成為無解路徑。
COMMON = ["docs/worklog/**", "docs/knowledge/errors.md", "docs/changes/**",
          "docs/acceptance/**", "docs/structure/**"]


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
