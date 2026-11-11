#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""G5 ACC + identity 閘門的行為測試。

**本檔不測「我寫的某個函式」，而是把 `.github/workflows/governance.yml` 裡 G5 步驟的
`run:` 腳本原封不動抽出來執行。** 這是刻意的：G5 是一段內嵌在 workflow 裡的 shell，
它沒有任何 Python 入口，所以在本檔出現之前，**它的判定邏輯無法在測試裡被驗證**——
跟 `CHG-20260810-06` 把 `scope_check` 的判定抽成純函式所解決的是同一個問題，
只是這次沒辦法抽成函式，那就直接跑那段腳本。

抽出來跑而不是在測試裡複製一份腳本，是為了讓「有人改了 workflow 卻沒改測試」必然紅燈。
複製一份只會讓兩邊各自漂移，而測試仍然綠。

守的是兩個已經發生過的缺陷（知識庫 K-011 / K-012）：

- **K-011**：`bash -e -o pipefail` 下 `A=$(ls … | head -1)` 在查無檔案時讓賦值失敗、
  `-e` 當場中止，於是下一行的 `::error::` 永遠印不出來。閘門擋對了，卻只留下一個裸的
  `exit code 2`。故本檔的斷言一律是「**非零退出 + 訊息真的印出來**」兩件事一起驗；
  只驗退出碼的測試會讓 K-011 原封不動地回來。
- **K-012**：`sed 's/.*: *//'` 的 `.*` 貪婪，吃到行內最後一個冒號，使房屋格式表頭
  （`- Date: … | Verifier: V1.5 | Branch: feat/x`）取出 `feat/x` 而非 `V1.5`，
  讓「驗收者與實作者同一人即紅」永遠不相等、從上線起空轉。故本檔**一律以房屋格式表頭**
  構造 fixture——用單冒號的簡化格式寫測試，正好會漏掉這個缺陷。

可直接執行：`python3 tests/gates/test_g5_acc_identity.py`
"""
import io
import os
import re
import subprocess
import sys
import tempfile
import unittest

# K-001 / K-004 同族：釘住輸出編碼，不依賴主控台 / locale 的 ambient 編碼。
for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
WORKFLOW = os.path.join(REPO_ROOT, ".github/workflows/governance.yml")

STEP_NAME = "G5 ACC + identity"

# GitHub Actions 的 `shell: bash` 預設展開式。**必須與實機一致**——
# 這幾個旗標正是 K-011 的成因，用別的旗標跑等於測了一個不存在的環境。
GH_BASH = ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail"]


def g5_script():
    """自 governance.yml 取出 G5 步驟的 run: 腳本原文。

    以**縮排**解析，不用 PyYAML。兩個理由：

    1. CI 的 `actions/setup-python@v5` 給的是乾淨的 Python，**沒有 PyYAML**。
       本機容器有（系統套件）、CI 沒有——本檔初版就是這樣在 CI 上 7 個案例全錯的
       （`ModuleNotFoundError: No module named 'yaml'`）。閘門的自我測試**不該**為了
       解析自己的 workflow 而給 CI 多一個依賴。
    2. `scripts/workflow_lint.py` 已經以縮排解析同一批 `run:` 區塊，並在該函式的
       docstring 寫明「刻意不用 YAML 解析」。多一套解析只會多一個漂移來源。
    """
    with io.open(WORKFLOW, encoding="utf-8") as fh:
        lines = fh.read().splitlines()

    # 步驟起點：`- name: G5 ACC + identity`
    start = step_indent = None
    for i, ln in enumerate(lines):
        if ln.strip() == "- name: " + STEP_NAME:
            start, step_indent = i, len(ln) - len(ln.lstrip())
            break
    assert start is not None, f"governance.yml 找不到步驟「{STEP_NAME}」"

    # 步驟終點：下一個同縮排（或更淺）的清單項目
    end = len(lines)
    for i in range(start + 1, len(lines)):
        ln = lines[i]
        if not ln.strip():
            continue
        if len(ln) - len(ln.lstrip()) <= step_indent and ln.strip().startswith("- "):
            end = i
            break
    body = lines[start:end]

    # shell 必須仍是 bash，否則 GH_BASH 的旗標假設失效（那會變成測一個不存在的環境）
    assert any(ln.strip() == "shell: bash" for ln in body), \
        f"{STEP_NAME} 的 shell 不再是 bash，本檔的 GH_BASH 假設已失效"

    # `run: |` 之後縮排更深的行即腳本本體
    run_at = next((i for i, ln in enumerate(body)
                   if re.match(r"run:\s*[|>]", ln.strip())), None)
    assert run_at is not None, f"{STEP_NAME} 沒有 `run: |` 區塊"
    run_indent = len(body[run_at]) - len(body[run_at].lstrip())
    script = [ln for ln in body[run_at + 1:]
              if not ln.strip() or (len(ln) - len(ln.lstrip())) > run_indent]
    pad = min((len(ln) - len(ln.lstrip()) for ln in script if ln.strip()), default=0)
    return "\n".join(ln[pad:] if ln.strip() else "" for ln in script)


def run_g5(tmp, slug="x"):
    """在 tmp 目錄下以 GitHub 的 shell 旗標執行 G5 腳本，回傳 (exit_code, 合併輸出)。"""
    proc = subprocess.run(
        GH_BASH + ["-c", g5_script()],
        cwd=tmp, capture_output=True, text=True,
        encoding="utf-8", errors="replace",          # K-009：明示編碼
        env={**os.environ, "GITHUB_HEAD_REF": f"feat/{slug}"},
    )
    return proc.returncode, proc.stdout + proc.stderr


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with io.open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def make_chg(tmp, slug="x", implementer="I1.1", header=True):
    """寫一份 CHG。header=True 用**房屋格式**的管線分欄表頭（K-012 的關鍵）。"""
    line = (f"- Date: 2026-11-11 | Branch: feat/{slug} | Risk: medium | "
            f"Implemented by: {implementer}\n") if header else "- Risk: medium\n"
    write(os.path.join(tmp, f"docs/changes/CHG-20261111-{slug}.md"), f"# CHG\n\n{line}")


def make_acc(tmp, slug="x", verifier="V1.1", result="pass", with_verifier=True):
    """寫一份 ACC。表頭刻意**把 Verifier 放在 Branch 之前**——與 repo 既有 ACC 一致，
    也正是 K-012 會取錯欄位的那個排列。"""
    head = (f"- Date: 2026-11-11 | Unit: X | Verifier: {verifier} | Branch: feat/{slug}\n"
            if with_verifier else f"- Date: 2026-11-11 | Unit: X | Branch: feat/{slug}\n")
    write(os.path.join(tmp, f"docs/acceptance/ACC-20261111-{slug}.md"),
          f"# ACC\n\n{head}\nResult: {result}\n")


class TestFailuresReportTheirReason(unittest.TestCase):
    """K-011：擋下來的時候，必須說得出為什麼。"""

    def test_missing_acc_reports_the_reason_not_just_an_exit_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            make_chg(tmp)                                    # 有 CHG，沒有 ACC
            code, out = run_g5(tmp)
            self.assertNotEqual(code, 0, "缺 ACC 必須擋下")
            # 這一行就是 K-011 的回歸斷言：拿掉 `|| true` 時，訊息會消失而只剩 exit 2。
            self.assertIn("medium 風險需要 ACC", out,
                          f"擋下了卻沒印出原因（K-011 復發）。實得輸出：{out!r}")

    def test_acc_not_marked_pass_reports_the_reason(self):
        with tempfile.TemporaryDirectory() as tmp:
            make_chg(tmp)
            make_acc(tmp, result="fail")
            code, out = run_g5(tmp)
            self.assertNotEqual(code, 0)
            self.assertIn("ACC 未判定 pass", out)

    def test_chg_without_implemented_by_reports_the_reason(self):
        with tempfile.TemporaryDirectory() as tmp:
            make_chg(tmp, header=False)                      # CHG 缺 Implemented by:
            make_acc(tmp)
            code, out = run_g5(tmp)
            self.assertNotEqual(code, 0)
            self.assertIn("CHG 缺 'Implemented by:'", out)


class TestMissingFieldsStayFailClosed(unittest.TestCase):
    """K-011 的第二面：`|| true` 不可以把 fail-closed 換成 fail-open。

    取值失敗會得到空字串，而空字串讓身分比對自動「不相等」——若沒有額外的空值檢查，
    一份**完全沒有 `Verifier:` 的 ACC** 會被放過。那不是訊息問題，是真的假綠燈。
    """

    def test_acc_without_verifier_line_is_rejected_rather_than_passed(self):
        with tempfile.TemporaryDirectory() as tmp:
            make_chg(tmp)
            make_acc(tmp, with_verifier=False)
            code, out = run_g5(tmp)
            self.assertNotEqual(
                code, 0,
                "沒有 Verifier 的 ACC 被放過了——空字串讓身分比對自動不相等（fail-open）")
            self.assertIn("ACC 缺 'Verifier:'", out)


class TestIdentityCheckActuallyFires(unittest.TestCase):
    """K-012：身分比對必須真的比對到「人」，而不是表頭的最後一欄。

    以下兩個案例都用**房屋格式表頭**（`Verifier:` 排在 `Branch:` 之前）。
    這個排列是關鍵：貪婪 sed 會取到 `feat/x`，使兩個案例**都**變成「不相等」而一起通過，
    於是下面第一個案例就是 K-012 的回歸斷言。
    """

    def test_self_acceptance_is_caught_in_the_house_header_format(self):
        with tempfile.TemporaryDirectory() as tmp:
            make_chg(tmp, implementer="I1.1")
            make_acc(tmp, verifier="I1.1")                   # 同一人：必須擋
            code, out = run_g5(tmp)
            self.assertNotEqual(
                code, 0,
                "驗收者與實作者同為 I1.1 卻被放過——取值取到表頭最後一欄了（K-012 復發）")
            self.assertIn("驗收者與實作者同一人", out)

    def test_independent_verifier_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            make_chg(tmp, implementer="I1.1")
            make_acc(tmp, verifier="V1.1")                   # 不同人：必須通過
            code, out = run_g5(tmp)
            self.assertEqual(code, 0, f"合法的獨立驗收被擋下了。實得輸出：{out!r}")


class TestEveryPipelineAssignmentIsGuarded(unittest.TestCase):
    """靜態防線：讓 K-011 不能靠「新增一行」悄悄回來。

    行為測試只涵蓋現有的失敗路徑；日後有人在這個步驟加一行新的
    `X=$(… | …)` 而忘了 `|| true`，那條新路徑會在第一次踩到時又變成裸 exit code。
    這條把規則本身釘住。
    """

    def test_no_unguarded_command_substitution_with_a_pipe(self):
        unguarded = [
            ln.strip() for ln in g5_script().splitlines()
            if "=$(" in ln and "|" in ln.split("=$(", 1)[1] and "|| true" not in ln
        ]
        self.assertEqual(
            unguarded, [],
            "這些賦值含管線但沒有 `|| true`：pipefail + -e 會讓它們靜默中止，"
            f"下一行的 ::error:: 永遠印不出來（K-011）。{unguarded}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
