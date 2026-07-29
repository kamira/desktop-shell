#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""相位閘門（G1b）：確保當前相位不會出現尚未允許的平台後端。

這是「Mac 期不准寫真實後端」從自制力變成機器強制的那一道。
沒有它，趕進度時的第一個動作就是寫個 cocoa 後端「先看到東西」，
而 kernel 抽象會在那一刻安靜地長成 macOS 的形狀。

E1-26 增強：「當前相位允許哪些後端」不再硬編碼，而是由
`docs/backlog/phase.json` 的 `phases`（相位定義 + 各相位後端白名單）驅動——
guard 依 phase.json 的當前相位取該相位的白名單強制之。
若 phase.json 沒有 `phases` 區塊，則回退到頂層 `allowed_backends`
（既有介面，向後相容）。CLI 介面、輸出、exit code 與既有行為一致：
`python scripts/backend_guard.py` 於相位 1、僅 null 後端存在時綠燈 exit 0。
"""
import json, os, sys, glob

# 契約測試禁止出現的平台分支 token（分平台就不再是契約，是兩套測試）。
PLATFORM_TOKENS = ("sys.platform", "platform.system", "win32", "darwin", "Cocoa", "ctypes.windll")


def repo_root():
    """回傳 repo 根目錄。測試可用環境變數 BACKEND_GUARD_ROOT 覆寫，
    既有 workflow 呼叫（無環境變數）行為不變。"""
    env = os.environ.get("BACKEND_GUARD_ROOT")
    if env:
        return os.path.abspath(env)
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_phase(root):
    """讀取並解析 docs/backlog/phase.json。"""
    with open(os.path.join(root, "docs/backlog/phase.json"), encoding="utf-8") as fh:
        return json.load(fh)


def allowed_backends(ph):
    """回傳當前相位允許的後端集合。

    來源優先序：
    1. phase.json 的 `phases[<當前相位>].allowed_backends`（相位驅動白名單，E1-26）。
    2. 若無 `phases` 區塊，回退到頂層 `allowed_backends`（既有介面，向後相容）。

    當前相位在 `phases` 中未定義時丟 KeyError，由呼叫端轉為紅燈。
    """
    phase = ph["phase"]
    phases = ph.get("phases")
    if phases is not None:
        key = str(phase)
        if key not in phases:
            raise KeyError(
                f"phase.json 未定義相位 {phase} 的後端白名單（phases[{key!r}] 不存在）"
            )
        return set(phases[key]["allowed_backends"])
    return set(ph["allowed_backends"])


def found_backends(root):
    """回傳 src/kernel/backend/ 下實際存在的後端目錄名集合。"""
    return {os.path.basename(p) for p in glob.glob(os.path.join(root, "src/kernel/backend/*"))
            if os.path.isdir(p)}


def check(root):
    """執行相位閘門。回傳 0（綠）或 1（紅）。輸出與既有 CI annotation 格式相容。"""
    ph = load_phase(root)
    phase = ph["phase"]

    try:
        allowed = allowed_backends(ph)
    except KeyError as e:
        print(f"::error::{e}")
        print("::error::請在 docs/backlog/phase.json 的 phases 區塊補上該相位的 allowed_backends")
        return 1

    found = found_backends(root)
    bad = found - allowed
    if bad:
        print(f"::error::相位 {phase} 只允許後端 {sorted(allowed)}，"
              f"但發現 {sorted(bad)}")
        print("::error::若確實要進入下一相位，請先更新 docs/backlog/phase.json 並開 CHG 記錄該決策")
        return 1

    # 反向檢查：契約測試不得包含平台分支
    for f in glob.glob(os.path.join(root, "tests/contract/**/*.py"), recursive=True):
        with open(f, encoding="utf-8") as fh:
            src = fh.read()
        for tok in PLATFORM_TOKENS:
            if tok in src:
                print(f"::error file={os.path.relpath(f, root)}::契約測試出現平台分支 `{tok}` —— "
                      "契約測試必須對所有後端一視同仁")
                return 1

    print(f"backend_guard OK — 相位 {phase}，後端 {sorted(found) or ['(尚無)']}")
    return 0


def main():
    return check(repo_root())


if __name__ == "__main__":
    sys.exit(main())
