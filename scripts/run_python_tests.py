#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""G0 閘門：跑遍 `tests/` 底下每一支 Python 測試。

## 為什麼不是一行 `unittest discover`

**因為它一個測試都收不到**：

    $ python -m unittest discover -s tests -p 'test_*.py'
    Ran 0 tests in 0.000s

`discover` 只遞迴進**套件**（有 `__init__.py` 的目錄）。本 repo 的 `tests/` 底下
是 `e1/`…`e12/`、`c1/`…`c4/`、`gates/`、`contract/`，沒有一個是套件——
`tests/` 直接放在 `-s` 會一個都收不到，`-t .` 則直接 ImportError。
加 `__init__.py` 沒有採用：那會把 `tests/` 變成 Python 套件樹，
而它底下絕大多數是 C++ 測試。

**關於 exit code 的一點更正**：初版的說法是「而且會安靜地回綠」——
那只在 **Python ≤ 3.11** 成立。3.12 起 `python -m unittest` 沒跑到任何測試時回
exit 5。本機是 3.11（回 0，真的是安靜的綠燈），CI 是 3.12（回 5，會紅）。
`tests/gates/test_run_python_tests.py` 已把兩種行為分別釘住。

無論回 0 或 5，「收不到測試」這件事不變，所以本腳本仍然自己走檔案樹。
而 0 個檔 / 0 個測試一律紅燈的規則也保留——那是本 repo 在 G3 防過兩次的
真空綠燈防線，不因為上游剛好也修了而省略。

## 為什麼需要它

2026-08-10 查出 CI 從未跑過任何 Python 測試：`tests/e1/test_backend_guard.py`
（14 個測試，守著相位閘門 G1b）沒有 `CMakeLists.txt`，CTest 收不到；
`governance.yml` 也沒有任何 pytest / unittest 呼叫。**守閘門的測試自己沒人守。**

`CHG-20260810-03` 讓 `tests/gates/` 進了 CI，但那是逐目錄點名——
下一個把測試放在別處的人一樣不會被跑到。本腳本改成掃全部，缺口才真的關上。

用法： run_python_tests.py [--dir tests]
"""
import argparse
import importlib.util
import os
import sys
import unittest

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_test_files(tests_dir):
    hits = []
    for dirpath, dirnames, filenames in os.walk(tests_dir):
        dirnames[:] = [d for d in dirnames if d not in ("__pycache__", ".pytest_cache")]
        for name in sorted(filenames):
            if name.startswith("test_") and name.endswith(".py"):
                hits.append(os.path.join(dirpath, name))
    return sorted(hits)


def load(path, tests_dir):
    """以檔案路徑載入模組。

    模組名帶上相對目錄（`e1.test_backend_guard`），避免不同目錄下的同名檔互相覆蓋——
    覆蓋的後果是「其中一支從此不再執行」，而且不會有任何錯誤訊息。
    """
    rel = os.path.relpath(path, tests_dir)
    modname = os.path.splitext(rel)[0].replace(os.sep, ".")
    spec = importlib.util.spec_from_file_location(modname, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[modname] = module
    spec.loader.exec_module(module)
    return module


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=os.path.join(ROOT, "tests"))
    a = ap.parse_args()
    tests_dir = os.path.abspath(a.dir)

    files = find_test_files(tests_dir)
    if not files:
        print("::error::%s 底下找不到任何 test_*.py——"
              "掃 0 個檔卻回綠，與沒有閘門同義（fail-closed）" % tests_dir)
        return 1

    print("找到 %d 支 Python 測試檔：" % len(files))
    for f in files:
        print("  %s" % os.path.relpath(f, ROOT).replace(os.sep, "/"))

    suite = unittest.TestSuite()
    loader = unittest.TestLoader()
    for f in files:
        try:
            suite.addTests(loader.loadTestsFromModule(load(f, tests_dir)))
        except Exception as exc:  # 匯入失敗＝測試沒跑，不能當成通過
            print("::error::載入 %s 失敗：%r" % (f, exc))
            return 1

    result = unittest.TextTestRunner(verbosity=2, stream=sys.stdout).run(suite)

    if result.testsRun == 0:
        print("::error::收到 %d 個檔卻跑了 0 個測試——同樣是真空綠燈" % len(files))
        return 1
    if not result.wasSuccessful():
        print("::error::Python 測試未全數通過（%d 失敗 / %d 錯誤 / 共 %d）"
              % (len(result.failures), len(result.errors), result.testsRun))
        return 1

    print("run_python_tests OK（%d 支檔、%d 個測試，跳過 %d）"
          % (len(files), result.testsRun, len(result.skipped)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
