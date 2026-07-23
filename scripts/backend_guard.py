#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""相位閘門：確保當前相位不會出現尚未允許的平台後端。

這是「Mac 期不准寫真實後端」從自制力變成機器強制的那一道。
沒有它，趕進度時的第一個動作就是寫個 cocoa 後端「先看到東西」，
而 kernel 抽象會在那一刻安靜地長成 macOS 的形狀。
"""
import json, os, sys, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ph = json.load(open(os.path.join(ROOT, "docs/backlog/phase.json"), encoding="utf-8"))
allowed = set(ph["allowed_backends"])

found = {os.path.basename(p) for p in glob.glob(os.path.join(ROOT, "src/kernel/backend/*"))
         if os.path.isdir(p)}
bad = found - allowed
if bad:
    print(f"::error::相位 {ph['phase']} 只允許後端 {sorted(allowed)}，"
          f"但發現 {sorted(bad)}")
    print("::error::若確實要進入下一相位，請先更新 docs/backlog/phase.json 並開 CHG 記錄該決策")
    sys.exit(1)

# 反向檢查：契約測試不得包含平台分支
for f in glob.glob(os.path.join(ROOT, "tests/contract/**/*.py"), recursive=True):
    src = open(f, encoding="utf-8").read()
    for tok in ("sys.platform", "platform.system", "win32", "darwin", "Cocoa", "ctypes.windll"):
        if tok in src:
            print(f"::error file={os.path.relpath(f, ROOT)}::契約測試出現平台分支 `{tok}` —— "
                  "契約測試必須對所有後端一視同仁")
            sys.exit(1)

print(f"backend_guard OK — 相位 {ph['phase']}，後端 {sorted(found) or ['(尚無)']}")
