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

# K-001 / K-004 同族：釘住輸出編碼，不依賴主控台/locale 的 ambient 編碼。
# 非 UTF-8 主控台（Windows cp1252 / cp950）印 CJK 會 UnicodeEncodeError，
# 而閘門「當掉」與閘門「擋下」在 exit code 上無法區分（CHG-20260810-05）。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

# 契約測試禁止出現的平台分支 token（分平台就不再是契約，是兩套測試）。
# 此組用於 Python 契約檔（既有行為，全文比對）。
PLATFORM_TOKENS = ("sys.platform", "platform.system", "win32", "darwin", "Cocoa", "ctypes.windll")

# C/C++ 契約檔的平台分支偵測（CHG-20260803-03 補上）。
#
# 為什麼不能沿用上面那組全文比對：契約測試的中文註解**本來就會寫**「不得出現 win32 / cocoa」，
# 全文比對會把這些自我約束的說明判成違規，反而逼人把註解刪掉。
# 因此 C/C++ 只檢查**前處理器指令行**（第一個非空白字元為 `#` 的行）——真正的平台分支
# 一定寫在 `#if` / `#ifdef` / `#include` 上，而註解行永遠不以 `#` 開頭，天然不會誤判。
CPP_PLATFORM_MACROS = ("_WIN32", "_WIN64", "__APPLE__", "__linux__", "__unix__",
                       "__MACH__", "_MSC_VER", "__MINGW32__")
CPP_PLATFORM_HEADERS = ("windows.h", "cocoa/", "appkit/", "unistd.h", "sys/", "x11/")
CPP_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx")


def cpp_platform_violation(src):
    """在 C/C++ 原始碼中找平台分支。回傳 (行號, 說明) 或 None。

    只看前處理器指令行，避開註解誤判（見 CPP_PLATFORM_MACROS 的說明）。
    """
    for lineno, line in enumerate(src.splitlines(), 1):
        stripped = line.lstrip()
        if not stripped.startswith("#"):
            continue
        directive = stripped[1:].lstrip()
        if directive.startswith("include"):
            lowered = directive.lower()
            for hdr in CPP_PLATFORM_HEADERS:
                if hdr in lowered:
                    return lineno, f"平台專屬標頭 `{hdr}`"
            continue
        if directive.split(None, 1)[:1] and directive.split(None, 1)[0] in (
                "if", "ifdef", "ifndef", "elif"):
            for macro in CPP_PLATFORM_MACROS:
                if macro in directive:
                    return lineno, f"平台條件編譯 `{macro}`"
    return None


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


# 平台專屬目錄的具名慣例。相位閘門原本只看 src/kernel/backend/*，
# 但相位 2 的 W1-02 系統匣後端落在 src/host/win32/——**完全不在閘門涵蓋範圍內**
# （CHG-20260803-06 §閘門涵蓋缺口）。平台碼不會只長在 kernel 底下，
# 故改為掃描 src/ 之下**任何**以平台命名的目錄。
PLATFORM_DIR_NAMES = ("win32", "cocoa", "wlroots", "x11", "wayland", "macos", "linux", "android")


def found_backends(root):
    """回傳 src/ 之下實際存在的平台目錄名集合。

    兩個來源：
      1. `src/kernel/backend/*` —— kernel 後端家族（含 `null`，既有行為）。
      2. `src/**/<平台名>/` —— 任何以平台命名的目錄（如 `src/host/win32/`）。
         沒有這一項的話，只要把平台碼放在 kernel/backend 之外就能繞過相位閘門。
    """
    found = {os.path.basename(p)
             for p in glob.glob(os.path.join(root, "src/kernel/backend/*"))
             if os.path.isdir(p)}

    src_root = os.path.join(root, "src")
    for dirpath, dirnames, _ in os.walk(src_root):
        for d in dirnames:
            if d.lower() in PLATFORM_DIR_NAMES:
                found.add(d.lower())
    return found


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
        print("::error::（掃描範圍：src/kernel/backend/* 與 src/ 之下任何以平台命名的目錄）")
        print("::error::若確實要進入下一相位，請先更新 docs/backlog/phase.json 並開 CHG 記錄該決策")
        return 1

    # 反向檢查：契約測試不得包含平台分支。
    #
    # CHG-20260803-03：原本只掃 `tests/contract/**/*.py`，但契約測試其實是 **C++**
    # （test_contract_backend.cpp / kernel_backend.hpp），該目錄下 .py 檔數為 0
    # —— 這道檢查從上線起就是空轉的。相位 2 放行 win32 後端的此刻，正是最容易
    # 不知不覺把契約測試改成遷就某個平台的時候，故一併補上 C/C++ 掃描。
    for f in sorted(glob.glob(os.path.join(root, "tests/contract/**/*"), recursive=True)):
        if not os.path.isfile(f):
            continue
        ext = os.path.splitext(f)[1].lower()
        if ext not in (".py",) + CPP_SUFFIXES:
            continue
        with open(f, encoding="utf-8") as fh:
            src = fh.read()
        rel = os.path.relpath(f, root).replace(os.sep, "/")

        if ext == ".py":
            for tok in PLATFORM_TOKENS:
                if tok in src:
                    print(f"::error file={rel}::契約測試出現平台分支 `{tok}` —— "
                          "契約測試必須對所有後端一視同仁")
                    return 1
            continue

        hit = cpp_platform_violation(src)
        if hit:
            lineno, what = hit
            print(f"::error file={rel},line={lineno}::契約測試出現{what} —— "
                  "契約測試必須對所有後端一視同仁；平台碼只能放在 src/kernel/backend/<name>/")
            return 1

    print(f"backend_guard OK — 相位 {phase}，後端 {sorted(found) or ['(尚無)']}")
    return 0


def main():
    return check(repo_root())


if __name__ == "__main__":
    sys.exit(main())
