#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""G6 halt_gate 測試。

這道閘門的整個價值在 **tighten-only**：政策沒有明確允許 AUTO 的，一律 HALT。
所以本檔的重點不是「AUTO 會不會亮」，而是**每一種「不知道」都必須落在 HALT 這一邊**——
政策檔不存在、停點沒定義、`auto` 清單缺漏、風險等級沒見過。
一道在不確定時放行的停點閘門，比沒有停點閘門更危險：它會讓人以為有守。

可直接執行：`python3 tests/gates/test_halt_gate.py`
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
SCRIPTS = os.path.join(REPO_ROOT, "scripts")
GATE_PATH = os.path.join(SCRIPTS, "halt_gate.py")

sys.path.insert(0, SCRIPTS)
import halt_gate as hg  # noqa: E402

POLICY = {"version": 1, "default": "halt",
          "gates": {"before_merge_or_release": {"auto": ["low"],
                                                "halt": ["medium", "high"]}}}


def run_cli(gate, risk, policy_file=None):
    env = dict(os.environ)
    if policy_file is not None:
        env["AI_SDLC_HALT_POLICY"] = policy_file
    proc = subprocess.run(
        [sys.executable, GATE_PATH, "--gate", gate, "--risk", risk],
        cwd=REPO_ROOT, env=env, capture_output=True, text=True,
        encoding="utf-8", errors="replace")
    return proc.returncode, proc.stdout + proc.stderr


def write_policy(obj):
    fd, path = tempfile.mkstemp(suffix=".json")
    with os.fdopen(fd, "w", encoding="utf-8") as fh:
        json.dump(obj, fh, ensure_ascii=False)
    return path


class TestTightenOnly(unittest.TestCase):
    """每一種「不知道」都必須是 HALT。"""

    def test_missing_policy_file_is_halt(self):
        # 設定缺席不是放行的理由。
        self.assertEqual(hg.decide(None, "before_merge_or_release", "low"), hg.HALT)

    def test_unknown_gate_is_halt(self):
        self.assertEqual(hg.decide(POLICY, "some_gate_nobody_defined", "low"), hg.HALT)

    def test_unknown_risk_is_halt(self):
        self.assertEqual(hg.decide(POLICY, "before_merge_or_release", "critical"), hg.HALT)

    def test_gate_without_auto_list_is_halt(self):
        p = {"gates": {"g": {"halt": ["low", "medium"]}}}   # 完全沒有 auto 鍵
        self.assertEqual(hg.decide(p, "g", "low"), hg.HALT)

    def test_empty_auto_list_is_halt(self):
        self.assertEqual(hg.decide({"gates": {"g": {"auto": []}}}, "g", "low"), hg.HALT)

    def test_empty_policy_object_is_halt(self):
        self.assertEqual(hg.decide({}, "before_merge_or_release", "low"), hg.HALT)

    def test_risk_in_halt_list_is_halt(self):
        self.assertEqual(hg.decide(POLICY, "before_merge_or_release", "medium"), hg.HALT)


class TestAuto(unittest.TestCase):
    def test_listed_risk_is_auto(self):
        self.assertEqual(hg.decide(POLICY, "before_merge_or_release", "low"), hg.AUTO)

    def test_exit_codes_are_0_and_10(self):
        # CI 靠 exit code 分辨 AUTO / HALT，數值本身是介面的一部分。
        self.assertEqual((hg.AUTO, hg.HALT), (0, 10))


class TestCLI(unittest.TestCase):
    def test_cli_low_is_auto(self):
        path = write_policy(POLICY)
        rc, out = run_cli("before_merge_or_release", "low", path)
        self.assertEqual(rc, 0, out)
        self.assertIn("AUTO", out)

    def test_cli_medium_is_halt(self):
        path = write_policy(POLICY)
        rc, out = run_cli("before_merge_or_release", "medium", path)
        self.assertEqual(rc, 10, out)
        self.assertIn("HALT", out)

    def test_cli_missing_policy_is_halt_with_warning(self):
        rc, out = run_cli("before_merge_or_release", "low",
                          os.path.join(tempfile.gettempdir(), "no_such_policy_file.json"))
        self.assertEqual(rc, 10, out)
        self.assertIn("::warning::", out)


class TestRealPolicy(unittest.TestCase):
    """真實 assets/halt_policy.json 必須與 AGENTS.md 宣稱的行為一致。"""

    def setUp(self):
        self.path = os.path.join(REPO_ROOT, "assets", "halt_policy.json")

    def test_policy_file_exists(self):
        self.assertTrue(os.path.exists(self.path), "政策檔不存在 → 每個單元都會 HALT")

    def test_real_policy_low_auto_medium_halt(self):
        with open(self.path, encoding="utf-8") as fh:
            policy = json.load(fh)
        g = "before_merge_or_release"
        self.assertEqual(hg.decide(policy, g, "low"), hg.AUTO)
        self.assertEqual(hg.decide(policy, g, "medium"), hg.HALT)
        self.assertEqual(hg.decide(policy, g, "high"), hg.HALT)

    def test_real_policy_declares_default_halt(self):
        with open(self.path, encoding="utf-8") as fh:
            policy = json.load(fh)
        self.assertEqual(policy.get("default"), "halt")


if __name__ == "__main__":
    unittest.main(verbosity=2)
