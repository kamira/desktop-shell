#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""階段閘門（NFR-08）：較早階段不得依賴較晚階段。

四個階段各配一個驗證器，階段的意義是「這批核心能力做完時，該階段的範例就能載入運作」。
若階段 A 的單元依賴階段 C 的單元，那個「A 做完就能驗收」的承諾就是假的——
A 的驗證器要等 C 才跑得起來，而 C 排在後面正是因為它現在沒有消費者。

修法只有一個方向：**被依賴者提前，而不是讓依賴者延後。**
若某個單元被更早階段消費，那它就不屬於它現在被歸的那個階段——
通常代表它是共用原語，只是當初以某個功能為動機才被歸進該功能群。
把依賴者往後推會讓早期階段愈縮愈小，最後驗證器全部擠到最後，等於沒有分階段。

用法：
  stage_check.py                 # 掃描整張相依圖（CI 用）
  stage_check.py --unit E11-02   # 只檢查單一單元的出向相依
"""
import argparse, json, os, sys

# K-001 / K-004 同族：釘住輸出編碼，不依賴主控台/locale 的 ambient 編碼。
# 非 UTF-8 主控台（Windows cp1252 / cp950）印 CJK 會 UnicodeEncodeError，
# 而閘門「當掉」與閘門「擋下」在 exit code 上無法區分（CHG-20260810-05）。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
U = {u["id"]: u for u in json.load(
    open(os.path.join(ROOT, "docs/backlog/units.json"), encoding="utf-8"))["units"]}

ORDER = {"A": 0, "B": 1, "C": 2, "D": 3}


def violations(units):
    """回傳 [(依賴者, 其階段, 被依賴者, 其階段)]，即依賴了更晚階段的組合。"""
    out = []
    for u in units:
        for d in u["depends_on"]:
            dep = U.get(d)
            if dep is None:                      # 相依完整性由別處把關，這裡不重複報
                continue
            if ORDER[dep["stage"]] > ORDER[u["stage"]]:
                out.append((u["id"], u["stage"], d, dep["stage"]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--unit", help="只檢查此單元（預設掃描全部）")
    a = ap.parse_args()

    if a.unit:
        if a.unit not in U:
            print(f"::error::未知工作單元 {a.unit}（不在 units.json）"); sys.exit(2)
        targets = [U[a.unit]]
    else:
        targets = list(U.values())

    missing = [u["id"] for u in targets if u.get("stage") not in ORDER]
    if missing:
        print(f"::error::{len(missing)} 個單元的 stage 欄位缺失或非法：{missing[:10]}")
        sys.exit(2)

    bad = violations(targets)
    if bad:
        print(f"::error::{len(bad)} 筆階段違規（較早階段依賴較晚階段）")
        for uid, us, did, ds in bad:
            print(f"::error::{uid}（階段 {us}）依賴 {did}（階段 {ds}）—— "
                  f"應把 {did} 提前到階段 {us}，不要把 {uid} 延後")
        sys.exit(1)

    print(f"stage_check OK — {len(targets)} 個單元無階段違規")


if __name__ == "__main__":
    main()
