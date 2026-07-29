#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""E1-26 相位閘門 backend_guard 測試。

涵蓋：相位 1 允許 null、相位 1 拒 win32/cocoa、phase.json 解析（相位驅動白名單）、
未知後端處理、向後相容回退、未定義相位、契約平台分支反向檢查，以及對「真實 repo 樹」的
既有行為回歸（相位 1、僅 null 後端 → 綠）。

可直接執行：`python3 tests/e1/test_backend_guard.py`
"""
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
SCRIPTS = os.path.join(REPO_ROOT, "scripts")
GUARD_PATH = os.path.join(SCRIPTS, "backend_guard.py")

sys.path.insert(0, SCRIPTS)
import backend_guard as bg  # noqa: E402


def make_tree(phase_json, backends=(), contract_files=None):
    """在暫存目錄搭一棵最小 repo 樹並回傳其根路徑。

    phase_json: dict，寫成 docs/backlog/phase.json。
    backends:   要建立的 src/kernel/backend/<name>/ 子目錄名。
    contract_files: {相對於 tests/contract/ 的檔名: 檔案內容} 的 dict。
    """
    root = tempfile.mkdtemp(prefix="bgtest_")
    backlog = os.path.join(root, "docs", "backlog")
    os.makedirs(backlog)
    with open(os.path.join(backlog, "phase.json"), "w", encoding="utf-8") as fh:
        json.dump(phase_json, fh, ensure_ascii=False)

    for name in backends:
        os.makedirs(os.path.join(root, "src", "kernel", "backend", name))

    if contract_files:
        cdir = os.path.join(root, "tests", "contract")
        os.makedirs(cdir)
        for rel, content in contract_files.items():
            path = os.path.join(cdir, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(content)
    return root


def run_check(root):
    """呼叫 bg.check 並回傳 (exit_code, stdout)。"""
    buf = io.StringIO()
    with redirect_stdout(buf):
        code = bg.check(root)
    return code, buf.getvalue()


# phase.json 含 phases 區塊（相位驅動白名單）的樣板。
PHASES = {
    "1": {"name": "Mac / 平台中立期", "allowed_backends": ["null"]},
    "2": {"name": "Windows 期", "allowed_backends": ["null", "win32"]},
    "3": {"name": "跨平台期", "allowed_backends": ["null", "win32", "cocoa"]},
}


def phase_json(phase, phases=PHASES, top_allowed=None):
    d = {"phase": phase}
    if top_allowed is not None:
        d["allowed_backends"] = top_allowed
    if phases is not None:
        d["phases"] = phases
    return d


class TestPhaseWhitelist(unittest.TestCase):
    def test_phase1_allows_null(self):
        root = make_tree(phase_json(1, top_allowed=["null"]), backends=["null"])
        code, out = run_check(root)
        self.assertEqual(code, 0, out)
        self.assertIn("backend_guard OK", out)
        self.assertIn("相位 1", out)

    def test_phase1_rejects_win32(self):
        root = make_tree(phase_json(1, top_allowed=["null"]), backends=["null", "win32"])
        code, out = run_check(root)
        self.assertEqual(code, 1, out)
        self.assertIn("win32", out)
        self.assertIn("::error::", out)

    def test_phase1_rejects_cocoa(self):
        root = make_tree(phase_json(1, top_allowed=["null"]), backends=["null", "cocoa"])
        code, out = run_check(root)
        self.assertEqual(code, 1, out)
        self.assertIn("cocoa", out)

    def test_phase_json_parsing_drives_whitelist(self):
        # 相位 2 由 phases 白名單允許 win32：同一棵樹在相位 1 紅、相位 2 綠。
        self.assertEqual(bg.allowed_backends(phase_json(1)), {"null"})
        ph2 = bg.allowed_backends(phase_json(2))
        self.assertEqual(ph2, {"null", "win32"})
        ph3 = bg.allowed_backends(phase_json(3))
        self.assertEqual(ph3, {"null", "win32", "cocoa"})

    def test_phase2_allows_win32(self):
        root = make_tree(phase_json(2, top_allowed=["null", "win32"]),
                         backends=["null", "win32"])
        code, out = run_check(root)
        self.assertEqual(code, 0, out)
        self.assertIn("相位 2", out)

    def test_phase2_still_rejects_cocoa(self):
        root = make_tree(phase_json(2, top_allowed=["null", "win32"]),
                         backends=["null", "win32", "cocoa"])
        code, out = run_check(root)
        self.assertEqual(code, 1, out)
        self.assertIn("cocoa", out)

    def test_unknown_backend_rejected(self):
        # 白名單外的未知後端（非 null/win32/cocoa）於相位 1 一律紅。
        root = make_tree(phase_json(1, top_allowed=["null"]),
                         backends=["null", "wayland"])
        code, out = run_check(root)
        self.assertEqual(code, 1, out)
        self.assertIn("wayland", out)

    def test_backward_compat_fallback_no_phases_block(self):
        # 舊格式 phase.json（無 phases 區塊）：回退到頂層 allowed_backends。
        legacy = {"phase": 1, "allowed_backends": ["null"]}
        self.assertEqual(bg.allowed_backends(legacy), {"null"})
        root = make_tree(legacy, backends=["null"])
        self.assertEqual(run_check(root)[0], 0)
        root_bad = make_tree(legacy, backends=["null", "win32"])
        self.assertEqual(run_check(root_bad)[0], 1)

    def test_undefined_phase_is_red(self):
        # phases 區塊存在但當前相位未定義 → 紅燈並提示補白名單。
        root = make_tree(phase_json(9), backends=["null"])
        code, out = run_check(root)
        self.assertEqual(code, 1, out)
        self.assertIn("::error::", out)

    def test_no_backend_dir_is_green(self):
        root = make_tree(phase_json(1, top_allowed=["null"]), backends=[])
        code, out = run_check(root)
        self.assertEqual(code, 0, out)
        self.assertIn("(尚無)", out)


class TestContractPlatformBranch(unittest.TestCase):
    def test_contract_platform_token_rejected(self):
        root = make_tree(
            phase_json(1, top_allowed=["null"]),
            backends=["null"],
            contract_files={"test_x.py": "import os\nif sys.platform == 'win32':\n    pass\n"},
        )
        code, out = run_check(root)
        self.assertEqual(code, 1, out)
        self.assertIn("平台分支", out)

    def test_contract_clean_is_green(self):
        root = make_tree(
            phase_json(1, top_allowed=["null"]),
            backends=["null"],
            contract_files={"test_x.py": "def test_ok():\n    assert True\n"},
        )
        self.assertEqual(run_check(root)[0], 0)


class TestRealRepoRegression(unittest.TestCase):
    """既有行為回歸：對真實 repo 樹，backend_guard 必須維持既有綠燈判定。"""

    def test_real_repo_check_is_green(self):
        code, out = run_check(REPO_ROOT)
        self.assertEqual(code, 0, out)
        self.assertIn("backend_guard OK", out)

    def test_cli_invocation_matches_workflow(self):
        # 既有 workflow 呼叫方式：python scripts/backend_guard.py（無參數、無環境覆寫）。
        env = dict(os.environ)
        env.pop("BACKEND_GUARD_ROOT", None)
        proc = subprocess.run(
            [sys.executable, GUARD_PATH],
            cwd=REPO_ROOT, env=env, capture_output=True, text=True,
        )
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("backend_guard OK", proc.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
