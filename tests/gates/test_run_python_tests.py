#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""G0 run_python_tests 的測試。

這支跑測試的東西自己也是檢查裝置的一部分，所以同樣要證明它的**紅燈可達**。
它最容易壞的方式不是報錯，是**安靜地跑 0 個測試然後回綠**——
`unittest discover -s tests` 就是這樣（見 `TestWhyNotPlainDiscover`）。

可直接執行：`python3 tests/gates/test_run_python_tests.py`
"""
import io
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
SCRIPTS = os.path.join(REPO_ROOT, "scripts")

sys.path.insert(0, SCRIPTS)
import run_python_tests as rpt  # noqa: E402

PASSING = ("import unittest\n"
           "class T(unittest.TestCase):\n"
           "    def test_ok(self):\n"
           "        self.assertTrue(True)\n")
FAILING = ("import unittest\n"
           "class T(unittest.TestCase):\n"
           "    def test_bad(self):\n"
           "        self.assertEqual(1, 2)\n")
NO_TESTS = "x = 1\n"
BROKEN_IMPORT = "import a_module_that_does_not_exist_anywhere\n"


def make_tree(files):
    d = tempfile.mkdtemp(prefix="g0test_")
    for rel, body in files.items():
        path = os.path.join(d, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(body)
    return d


def run(tree):
    argv = sys.argv
    sys.argv = ["run_python_tests.py", "--dir", tree]
    buf = io.StringIO()
    try:
        with redirect_stdout(buf):
            code = rpt.main()
    finally:
        sys.argv = argv
    return code, buf.getvalue()


class TestRedLightReachable(unittest.TestCase):
    def test_failing_test_is_red(self):
        code, out = run(make_tree({"e1/test_x.py": FAILING}))
        self.assertEqual(code, 1, out)

    def test_zero_files_is_red(self):
        # 掃 0 個檔卻回綠 = 與沒有閘門同義。
        code, out = run(make_tree({"e1/notatest.py": PASSING}))
        self.assertEqual(code, 1, out)
        self.assertIn("fail-closed", out)

    def test_files_but_zero_tests_is_red(self):
        # 收到檔案卻一個測試都沒收到——真空綠燈的第二種長相。
        code, out = run(make_tree({"e1/test_empty.py": NO_TESTS}))
        self.assertEqual(code, 1, out)
        self.assertIn("0 個測試", out)

    def test_import_error_is_red(self):
        # 匯入失敗 = 測試沒跑，不能當成通過。
        code, out = run(make_tree({"e1/test_broken.py": BROKEN_IMPORT}))
        self.assertEqual(code, 1, out)
        self.assertIn("載入", out)


class TestGreenLight(unittest.TestCase):
    def test_passing_tests_are_green(self):
        code, out = run(make_tree({"e1/test_a.py": PASSING, "gates/test_b.py": PASSING}))
        self.assertEqual(code, 0, out)
        self.assertIn("run_python_tests OK", out)

    def test_recurses_into_non_package_dirs(self):
        # 這正是 unittest discover 做不到的事：子目錄沒有 __init__.py。
        tree = make_tree({"e1/test_a.py": PASSING, "c4/deep/test_b.py": PASSING})
        self.assertEqual(len(rpt.find_test_files(tree)), 2)

    def test_same_basename_in_two_dirs_both_run(self):
        # 模組名若不帶目錄，後載入的會覆蓋先載入的——而且不會有任何錯誤訊息。
        tree = make_tree({"e1/test_dup.py": PASSING, "e2/test_dup.py": PASSING})
        code, out = run(tree)
        self.assertEqual(code, 0, out)
        self.assertIn("2 個測試", out)


class TestWhyNotPlainDiscover(unittest.TestCase):
    """釘住「為什麼不直接用 unittest discover」——這個理由值得被測試守住。

    **真正不變的理由是「它收不到任何測試」**，與 Python 版本無關：
    `discover` 只遞迴進套件，而本 repo 的 `tests/` 子目錄都沒有 `__init__.py`。

    「而且會安靜地回綠」這半句**只在 Python ≤ 3.11 成立**。3.12 起
    `python -m unittest` 在一個測試都沒跑到時回 exit 5。初版把 exit 0 寫死成斷言，
    在 CI（3.12）當場紅燈——是這道閘門抓到作者自己把版本相依的行為當成了通則。
    保留這條測試，但斷言改成兩個版本都涵蓋。
    """

    def test_plain_discover_finds_nothing(self):
        tree = make_tree({"e1/test_a.py": PASSING})
        proc = subprocess.run(
            [sys.executable, "-m", "unittest", "discover", "-s", tree, "-p", "test_*.py"],
            capture_output=True, text=True, encoding="utf-8", errors="replace")

        # 版本無關的部分：一個測試都收不到。
        self.assertIn("Ran 0 tests", proc.stdout + proc.stderr)

        # 版本相依的部分：≤3.11 回 0（安靜的綠燈），3.12+ 回 5。
        if sys.version_info >= (3, 12):
            self.assertEqual(proc.returncode, 5, "3.12+ 應以 exit 5 表示沒跑到測試")
        else:
            self.assertEqual(proc.returncode, 0, "≤3.11 回 0——0 個測試卻是綠的")

        code, _ = run(tree)                     # 同一棵樹，G0 收得到
        self.assertEqual(code, 0)
        self.assertEqual(len(rpt.find_test_files(tree)), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
