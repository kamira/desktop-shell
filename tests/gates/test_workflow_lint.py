#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""G8 workflow_lint 閘門測試。

**最重要的一條在 `TestTheRealRegression`**：拿 2026-08-10 修復前的真實 workflow
（G2 直接內插 PR 內文的那一版）餵給 lint，必須紅。合成測資能證明邏輯正確，
只有真實那一版能證明「這道閘門確實會擋住當初真的發生過的那件事」。

可直接執行：`python3 tests/gates/test_workflow_lint.py`
"""
import os
import io
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
import workflow_lint as wl  # noqa: E402

# 修復前 G2 的真實寫法（PR #210 的內文就是被這一行執行掉的）。
VULNERABLE = """\
name: t
on: [pull_request]
jobs:
  gate:
    runs-on: ubuntu-latest
    steps:
      - name: G2 CHG linked
        shell: bash
        run: |
          echo "${{ github.event.pull_request.body }}" | grep -qE 'CHG-[0-9]{8}-' \\
            || { echo "::error::PR 內文缺少 CHG- 參照"; exit 1; }
"""

FIXED = """\
name: t
on: [pull_request]
jobs:
  gate:
    runs-on: ubuntu-latest
    steps:
      - name: G2 CHG linked
        shell: bash
        env:
          PR_BODY: ${{ github.event.pull_request.body }}
        run: |
          printf '%s' "$PR_BODY" | grep -qE 'CHG-[0-9]{8}-' \\
            || { echo "::error::PR 內文缺少 CHG- 參照"; exit 1; }
"""


def lint(text=None, files=None):
    d = tempfile.mkdtemp(prefix="g8test_")
    # `files if files is not None`——不能用 `files or`：空 dict 是 falsy，
    # 會讓「掃 0 個檔」那條測試偷偷變成掃一個 None 內容的檔。
    for name, body in (files if files is not None else {"w.yml": text}).items():
        with open(os.path.join(d, name), "w", encoding="utf-8") as fh:
            fh.write(body)
    buf = io.StringIO()
    with redirect_stdout(buf):
        code = wl.check(d)
    return code, buf.getvalue()


class TestRedLightReachable(unittest.TestCase):
    def test_pr_body_interpolation_is_red(self):
        code, out = lint(VULNERABLE)
        self.assertEqual(code, 1, out)
        self.assertIn("github.event.pull_request.body", out)

    def test_pr_title_interpolation_is_red(self):
        # 黑名單思維會漏掉 title；允許清單天生擋得住。
        code, out = lint(VULNERABLE.replace("pull_request.body", "pull_request.title"))
        self.assertEqual(code, 1, out)

    def test_head_ref_is_red(self):
        # git 允許分支名含 $ 與反引號，所以 ref 名內插同樣是注入面。
        code, out = lint(VULNERABLE.replace("github.event.pull_request.body",
                                            "github.head_ref"))
        self.assertEqual(code, 1, out)

    def test_single_line_run_is_red(self):
        y = ("name: t\non: [pull_request]\njobs:\n  g:\n    runs-on: u\n    steps:\n"
             "      - run: echo ${{ github.event.pull_request.body }}\n")
        self.assertEqual(lint(y)[0], 1)

    def test_empty_dir_is_red(self):
        # 掃 0 個檔卻回綠 = 與沒有閘門同義。
        code, out = lint(files={})
        self.assertEqual(code, 1, out)
        self.assertIn("fail-closed", out)


class TestGreenLight(unittest.TestCase):
    def test_env_indirection_is_green(self):
        code, out = lint(FIXED)
        self.assertEqual(code, 0, out)
        self.assertIn("workflow_lint OK", out)

    def test_safe_expressions_allowed(self):
        y = ("name: t\non: [pull_request]\njobs:\n  g:\n    runs-on: u\n    steps:\n"
             "      - run: gh pr merge ${{ github.event.pull_request.number }} --squash\n")
        self.assertEqual(lint(y)[0], 0)

    def test_env_block_interpolation_not_flagged(self):
        # env: 區塊裡的 ${{ }} 正是**建議寫法**，不得被自己的閘門擋下。
        self.assertNotIn("PR_BODY", lint(FIXED)[1])

    def test_step_outputs_allowed(self):
        y = ("name: t\non: [pull_request]\njobs:\n  g:\n    runs-on: u\n    steps:\n"
             "      - run: python scripts/plan.py gate ${{ steps.unit.outputs.id }}\n")
        self.assertEqual(lint(y)[0], 0)


class TestAllowComment(unittest.TestCase):
    """豁免必須貼在違規行本身或其上一行——不是貼在步驟開頭就整步免疫。

    範圍放寬到整個步驟的話，一句理由會替它底下所有內插背書，
    包括日後才加進來、沒人審過的那幾行。
    """

    OFFENDING = '          echo "${{ github.event.pull_request.body }}"'

    def test_allow_with_reason_passes(self):
        y = VULNERABLE.replace(
            self.OFFENDING,
            "          # workflow-lint: allow 測試用途，值已於上游驗證\n" + self.OFFENDING)
        code, out = lint(y)
        self.assertEqual(code, 0, out)

    def test_allow_without_reason_still_red(self):
        # 空白的豁免等於沒有簽名。
        y = VULNERABLE.replace(
            self.OFFENDING,
            "          # workflow-lint: allow\n" + self.OFFENDING)
        self.assertEqual(lint(y)[0], 1)

    def test_allow_on_a_distant_line_does_not_cover(self):
        # 貼在步驟開頭（離違規行兩行以上）不算數。
        y = VULNERABLE.replace(
            "        run: |\n",
            "        # workflow-lint: allow 想一次赦免整步\n        run: |\n")
        self.assertEqual(lint(y)[0], 1)


class TestTheRealRegression(unittest.TestCase):
    """本 repo 的真實 workflow：修復後必須綠。

    這條同時是「修復真的落地了」的持續守護——有人把 env: 改回內插就會紅。
    """

    def test_current_repo_workflow_is_clean(self):
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = wl.check(os.path.join(REPO_ROOT, ".github", "workflows"))
        self.assertEqual(code, 0, buf.getvalue())


if __name__ == "__main__":
    unittest.main(verbosity=2)
