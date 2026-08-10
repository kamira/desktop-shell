#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""G7 status_check 閘門測試。

**紅燈可達性是本檔的主要職責。** 只證明綠燈會亮，等於沒有證明閘門有效——
一個永遠回 0 的檢查與沒有檢查在 exit code 上無法區分。所以三種漂移寫法
（Proposed / 沒有 Status 段 / Status 段空白）都各有一條斷言它**確實紅**。

另有一組對齊測試：本閘門的判定必須與 ai-sdlc `session_start.py` 的
`status_scope` 語意逐字一致。兩套判定若分歧，等於又多一個漂移來源。

可直接執行：`python3 tests/gates/test_status_check.py`
"""
import os
import io
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
CHECK_PATH = os.path.join(SCRIPTS, "status_check.py")

sys.path.insert(0, SCRIPTS)
import status_check as sc  # noqa: E402

ACCEPTED_BODY = "# CHG-20260810-99 — 測試用\n\n## Status\n\nAccepted — self-verified。\n"
PROPOSED_BODY = "# CHG-20260810-99 — 測試用\n\n## Status\n\nProposed — 未 push、未開 PR。\n"
NO_STATUS_BODY = "# CHG-20260810-99 — 測試用\n\n## Motivation\n\n做了一些事。\n"
EMPTY_STATUS_BODY = "# CHG-20260810-99 — 測試用\n\n## Status\n\n## Verification\n\n跑過了。\n"
PAUSED_BODY = "# CHG-20260810-99 — 測試用\n\n## Status\n\nPaused — 等上游決策。\n"


def make_repo(chg_files):
    """搭一棵最小 git repo，main 上無 CHG，工作分支加上 chg_files。"""
    root = tempfile.mkdtemp(prefix="g7test_")

    def git(*a):
        return subprocess.run(["git", "-C", root, *a], capture_output=True, text=True)

    git("init", "-q", "-b", "main")
    git("config", "user.email", "t@t"); git("config", "user.name", "t")
    os.makedirs(os.path.join(root, "docs", "changes"))
    with open(os.path.join(root, "README.md"), "w", encoding="utf-8") as fh:
        fh.write("seed\n")
    git("add", "-A"); git("commit", "-qm", "seed")
    git("checkout", "-q", "-b", "work")
    for name, body in chg_files.items():
        with open(os.path.join(root, "docs", "changes", name), "w", encoding="utf-8") as fh:
            fh.write(body)
    if chg_files:
        git("add", "-A"); git("commit", "-qm", "add chg")
    return root


def run(root, base="main", body=""):
    buf = io.StringIO()
    with redirect_stdout(buf):
        code = sc.check(base, body, root=root)
    return code, buf.getvalue()


class TestRedLightReachable(unittest.TestCase):
    """閘門必須真的會紅——這組測試若全綠而其他組也全綠，閘門就是壞的。"""

    def test_proposed_is_red(self):
        root = make_repo({"CHG-20260810-99.md": PROPOSED_BODY})
        code, out = run(root)
        self.assertEqual(code, 1, out)
        self.assertIn("::error::", out)
        self.assertIn("proposed", out.lower())

    def test_missing_status_section_is_red(self):
        # 98 筆漂移裡最多的一種寫法：根本沒有 ## Status 段。
        root = make_repo({"CHG-20260810-99.md": NO_STATUS_BODY})
        code, out = run(root)
        self.assertEqual(code, 1, out)
        self.assertIn("完全沒有", out)

    def test_empty_status_section_is_red(self):
        root = make_repo({"CHG-20260810-99.md": EMPTY_STATUS_BODY})
        code, out = run(root)
        self.assertEqual(code, 1, out)

    def test_one_bad_among_good_is_red(self):
        # 批次 PR 動了很多 CHG：只要有一份沒收尾就必須紅，不得被多數綠燈稀釋。
        root = make_repo({"CHG-20260810-97.md": ACCEPTED_BODY,
                          "CHG-20260810-98.md": ACCEPTED_BODY,
                          "CHG-20260810-99.md": PROPOSED_BODY})
        code, out = run(root)
        self.assertEqual(code, 1, out)
        self.assertIn("CHG-20260810-99.md", out)
        self.assertNotIn("CHG-20260810-97.md ——", out)


class TestGreenLight(unittest.TestCase):
    def test_accepted_is_green(self):
        root = make_repo({"CHG-20260810-99.md": ACCEPTED_BODY})
        code, out = run(root)
        self.assertEqual(code, 0, out)
        self.assertIn("status_check OK", out)

    def test_paused_is_green(self):
        # 刻意擱置的 WIP 是合法狀態，不是壞掉——擋它會逼人把 Proposed 謊報成 Accepted。
        root = make_repo({"CHG-20260810-99.md": PAUSED_BODY})
        self.assertEqual(run(root)[0], 0)

    def test_chinese_closed_wording_is_green(self):
        root = make_repo({"CHG-20260810-99.md": "# x\n\n## 狀態\n\n已驗收——見 ACC。\n"})
        self.assertEqual(run(root)[0], 0)


class TestFallbackToPRBody(unittest.TestCase):
    """PR 沒動任何 CHG 檔時，退回讀 PR 內文（G2 已保證內文有 CHG 參照）。"""

    def test_body_reference_checked(self):
        root = make_repo({})  # work 分支沒動 docs/changes/
        os.makedirs(os.path.join(root, "docs", "changes"), exist_ok=True)
        with open(os.path.join(root, "docs", "changes", "CHG-20260810-99.md"),
                  "w", encoding="utf-8") as fh:
            fh.write(PROPOSED_BODY)  # 既有檔，未被本 PR 異動
        code, out = run(root, body="本 PR 實作 CHG-20260810-99。")
        self.assertEqual(code, 1, out)

    def test_body_reference_accepted_is_green(self):
        root = make_repo({})
        with open(os.path.join(root, "docs", "changes", "CHG-20260810-99.md"),
                  "w", encoding="utf-8") as fh:
            fh.write(ACCEPTED_BODY)
        code, out = run(root, body="本 PR 實作 CHG-20260810-99。")
        self.assertEqual(code, 0, out)

    def test_suffixed_filename_resolved(self):
        # 檔名可能帶後綴，如 CHG-20260803-04-w1_01.md。
        root = make_repo({})
        with open(os.path.join(root, "docs", "changes", "CHG-20260810-99-w9_09.md"),
                  "w", encoding="utf-8") as fh:
            fh.write(ACCEPTED_BODY)
        code, out = run(root, body="見 CHG-20260810-99。")
        self.assertEqual(code, 0, out)


class TestFailClosed(unittest.TestCase):
    """查不到不等於沒問題——三種「查無」都必須紅。"""

    def test_nothing_to_check_is_red(self):
        root = make_repo({})
        code, out = run(root, body="這份 PR 內文忘了寫 CHG 參照")
        self.assertEqual(code, 1, out)
        self.assertIn("fail-closed", out)

    def test_body_references_nonexistent_chg_is_red(self):
        root = make_repo({})
        code, out = run(root, body="見 CHG-20991231-01。")
        self.assertEqual(code, 1, out)
        self.assertIn("不存在", out)


class TestAlignmentWithHook(unittest.TestCase):
    """判定語意必須與 ai-sdlc session_start.py 逐字一致，否則又多一個漂移來源。"""

    def test_keyword_sets_match_hook(self):
        self.assertEqual(sc.ACCEPTED, ("已驗收", "已收尾", "accepted", "closed"))
        self.assertEqual(sc.PAUSED, ("暫停", "paused"))

    def test_only_first_line_of_status_counts(self):
        # hook 刻意只取第一行結論：狀態節後方的補充說明不得影響判定。
        text = ("# x\n\n## Status\n\nProposed — 待補。\n\n"
                "> 備註：本項已驗收的部分僅限文件。\n")
        self.assertFalse(sc.is_closed(text))

    def test_status_scope_stops_at_next_heading(self):
        text = "# x\n\n## Status\n\nProposed。\n\n## Verification\n\n已驗收\n"
        self.assertFalse(sc.is_closed(text))


class TestRealRepoRegression(unittest.TestCase):
    """真實 repo 回歸：全庫 CHG 跑一次判定。

    **刻意預設不跑（需 `G7_FULL_SCAN=1`）。** 它是有用的本機健檢，但拿來當 CI 阻擋條件
    會過度殺傷：一張**合法**停在 `Proposed` 的 CHG（例如正等 confirm 停點裁決的那種，
    本 CHG 自己就當過一小時）會讓**所有人的 PR** 一起紅——與該 PR 毫無關係。
    防止新漂移是 G7 的逐 PR 檢查在做，不需要全庫掃描來重複。
    """

    @unittest.skipUnless(os.environ.get("G7_FULL_SCAN") == "1",
                         "全庫掃描預設關閉：設 G7_FULL_SCAN=1 手動執行")
    def test_all_committed_chgs_are_closed(self):
        import glob
        bad = []
        for p in sorted(glob.glob(os.path.join(REPO_ROOT, "docs", "changes", "CHG-*.md"))):
            with open(p, encoding="utf-8", errors="ignore") as fh:
                if not sc.is_closed(fh.read()):
                    bad.append(os.path.basename(p))
        self.assertEqual(bad, [], "以下 CHG 未收尾：%s" % bad)

    def test_cli_runs(self):
        proc = subprocess.run([sys.executable, CHECK_PATH, "--base", "HEAD"],
                              cwd=REPO_ROOT, capture_output=True, text=True,
                              encoding="utf-8", errors="replace")
        # --base HEAD → 無異動檔、無內文 → fail-closed 紅燈。確認 CLI 不會當掉。
        self.assertIn(proc.returncode, (0, 1), proc.stdout + proc.stderr)
        self.assertNotIn("Traceback", proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
