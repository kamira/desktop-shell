#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""G1 scope_check 測試。

這道閘門把「不得自行擴權」變成機器強制，所以最重要的斷言是**越權真的會被抓到**，
而不是「合法的檔案會通過」。後者失效只會讓人多問一句，前者失效沒有任何人會發現。

另有一組 `TestFnmatchRealBehaviour`：釘住 `fnmatch` 的實際語意（`*` 會跨 `/`）。
那不是本閘門刻意設計的行為，但它**現在就是判定的一部分**——沒有測試釘著，
任何人「順手把 fnmatch 換成更嚴謹的比對」都會在不知情的狀況下改變 187 個單元的鎖定範圍。

可直接執行：`python3 tests/gates/test_scope_check.py`
"""
import json
import os
import subprocess
import sys
import unittest

for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
SCRIPTS = os.path.join(REPO_ROOT, "scripts")
CHECK_PATH = os.path.join(SCRIPTS, "scope_check.py")

sys.path.insert(0, SCRIPTS)
import scope_check as sc  # noqa: E402

UNIT_SCOPE = ["src/kernel/e1_01/**", "tests/e1/test_e1_01*",
              "docs/changes/CHG-*-e1_01.md"]
ALLOW = UNIT_SCOPE + sc.COMMON


class TestOutOfScopeIsCaught(unittest.TestCase):
    def test_foreign_source_file_is_caught(self):
        bad = sc.out_of_scope(["src/kernel/e1_02/input_strategy.cpp"], ALLOW)
        self.assertEqual(bad, ["src/kernel/e1_02/input_strategy.cpp"])

    def test_root_cmakelists_is_caught(self):
        # 根 CMakeLists 不在任何單元的 write_scope 內，這是刻意的。
        self.assertEqual(sc.out_of_scope(["CMakeLists.txt"], ALLOW), ["CMakeLists.txt"])

    def test_directory_md_is_caught_but_structure_units_is_not(self):
        # docs/structure/** 在 COMMON 內，所以兩者都通過——
        # 「不得動 directory.md」是稽核紀律，不是本閘門擋的。這條把界線寫清楚。
        self.assertEqual(sc.out_of_scope(["docs/structure/directory.md"], ALLOW), [])
        self.assertEqual(sc.out_of_scope(["docs/structure/units/e1_01.md"], ALLOW), [])

    def test_one_bad_among_many_good_is_caught(self):
        files = ["src/kernel/e1_01/a.cpp", "tests/e1/test_e1_01.cpp",
                 "src/host/win32/tray.cpp"]
        self.assertEqual(sc.out_of_scope(files, ALLOW), ["src/host/win32/tray.cpp"])

    def test_workflow_file_is_caught(self):
        # 單元分支改 CI 設定 = 改動守自己的那道閘門，必須擋。
        self.assertEqual(sc.out_of_scope([".github/workflows/governance.yml"], ALLOW),
                         [".github/workflows/governance.yml"])


class TestInScope(unittest.TestCase):
    def test_unit_source_passes(self):
        self.assertEqual(sc.out_of_scope(["src/kernel/e1_01/layer_stack.cpp"], ALLOW), [])

    def test_nested_unit_source_passes(self):
        self.assertEqual(sc.out_of_scope(["src/kernel/e1_01/detail/impl.cpp"], ALLOW), [])

    def test_common_paths_pass(self):
        files = ["docs/worklog/I1.1-e1_01.md", "docs/knowledge/errors.md",
                 "docs/changes/CHG-20260724-e1_01.md",
                 "docs/acceptance/ACC-20260724-e1_01.md",
                 "docs/structure/units/e1_01.md"]
        self.assertEqual(sc.out_of_scope(files, ALLOW), [])

    def test_knowledge_index_is_not_common(self):
        # COMMON 只放行 errors.md，不放行 INDEX.md——INDEX.md 自己就是這樣寫的。
        self.assertEqual(sc.out_of_scope(["docs/knowledge/INDEX.md"], ALLOW),
                         ["docs/knowledge/INDEX.md"])

    def test_empty_diff_passes(self):
        self.assertEqual(sc.out_of_scope([], ALLOW), [])


class TestFnmatchRealBehaviour(unittest.TestCase):
    """釘住現行語意，不是主張它是對的。

    `fnmatch` 的 `*` **會跨 `/`**，所以 `content/**` 這種較淺的 write_scope
    實際上放行整棵子樹。真實 units.json 裡有 5 個單元的 scope 只有一層深度。
    這件事寫在測試裡，是為了讓下一個想「換成更嚴謹的比對」的人先看到影響面。
    """

    def test_star_crosses_slash(self):
        self.assertTrue(sc.in_scope("content/a/b/c.cpp", ["content/**"]))
        self.assertTrue(sc.in_scope("content/x.cpp", ["content/**"]))

    def test_prefix_must_still_match_literally(self):
        # 至少前綴是實的：content_evil/ 不會被 content/** 放行。
        self.assertFalse(sc.in_scope("content_evil/x.cpp", ["content/**"]))

    def test_trailing_star_pattern(self):
        self.assertTrue(sc.in_scope("tests/e1/test_e1_01_extra.cpp",
                                    ["tests/e1/test_e1_01*"]))
        self.assertFalse(sc.in_scope("tests/e1/test_e1_02.cpp",
                                     ["tests/e1/test_e1_01*"]))


class TestCLI(unittest.TestCase):
    def test_unknown_unit_exits_2(self):
        # 2 而非 1：未知單元是「問錯問題」，與「答案是越權」要分得開。
        proc = subprocess.run(
            [sys.executable, CHECK_PATH, "--unit", "E9-999", "--base", "HEAD"],
            cwd=REPO_ROOT, capture_output=True, text=True,
            encoding="utf-8", errors="replace")
        self.assertEqual(proc.returncode, 2, proc.stdout + proc.stderr)
        self.assertIn("未知工作單元", proc.stdout + proc.stderr)

    def test_no_diff_against_head_is_green(self):
        proc = subprocess.run(
            [sys.executable, CHECK_PATH, "--unit", "E1-01", "--base", "HEAD"],
            cwd=REPO_ROOT, capture_output=True, text=True,
            encoding="utf-8", errors="replace")
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("scope_check OK", proc.stdout)


class TestRealUnits(unittest.TestCase):
    def test_every_unit_has_a_non_empty_write_scope(self):
        # 空的 write_scope 會讓該單元的任何檔案都算越權——無解路徑。
        empty = [uid for uid, u in sc.U.items() if not u.get("write_scope")]
        self.assertEqual(empty, [])

    def test_common_list_matches_documented_set(self):
        self.assertEqual(sorted(sc.COMMON), sorted([
            "docs/worklog/**", "docs/knowledge/errors.md", "docs/changes/**",
            "docs/acceptance/**", "docs/structure/**"]))

    def test_units_json_loaded(self):
        with open(os.path.join(REPO_ROOT, "docs/backlog/units.json"), encoding="utf-8") as fh:
            self.assertEqual(len(sc.U), len(json.load(fh)["units"]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
