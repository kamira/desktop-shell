#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`G3b 註冊測試數下界` 的行為測試。

**本檔把 `.github/workflows/governance.yml` 裡 G3b 步驟的判定邏輯原封不動抽出來執行。**
理由與 `tests/gates/test_g5_acc_identity.py` / `test_sanitizer_gate_checks.py` 相同：
那是一段內嵌在 workflow 裡的 shell，沒有任何 Python 入口，所以在本檔出現之前它的判定
無法在測試裡被驗證 —— 而一道沒有自我測試的防線，正是 `K-011` 整條記載的那件事。

**G3b 本身的由來值得記在這裡**：它補的是另一道防線的洞。`CHG-20261112-04` 新增
`gate_sanitizers` 的註冊數一致性檢查時，把它寫成「真空綠燈防線」並強調比「大於 0」強。
H1-06 驗收時實測（V1.5 查出、I1 複現）：把一個單元的 `add_test` 包進 `if(WIN32)`，
兩個 ubuntu 建置**都**排除它 → 一致性檢查相等而放行、G3 的 `>0` 通過、`gate_windows`
的硬編碼名單不含它 —— **三道全綠而該單元在 ubuntu 的覆蓋整個消失**。
一致性檢查抓的是「兩個建置分歧」，不是「兩邊一起變少」；那個措辭過滿。
G3b 抓的就是後者。

守的是 G3b 可能空轉的四個方式：

1. **管線賦值少了 `|| true`** → `pipefail` + `-e` 下賦值失敗當場中止，`::error::` 印不出來（`K-011`）。
2. **取不到數字時放行** → `|| true` 的代價是空字串，而空字串在 `-lt` 比較下會出錯或被當 0；
   少了 fail-closed 檢查就會變成 fail-open（`K-012` 同族）。
3. **豁免標記沒有錨在行首** → 任何在 PR 內文**提到**這個標記的散文都會意外開後門。
4. **PR 內文被內插進腳本** → `K-008`（2026-08-10 真的發生過）。G8 已擋，本檔再釘一次
   「必須經 env 取用」，因為這一條的代價是命令注入。

可直接執行：`python3 tests/gates/test_test_count_floor.py`
"""
import io
import os
import re
import subprocess
import sys
import unittest

# K-001 / K-004 同族：釘住輸出編碼，不依賴主控台 / locale 的 ambient 編碼。
for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
WORKFLOW = os.path.join(REPO_ROOT, ".github/workflows/governance.yml")

STEP_NAME = "G3b 註冊測試數下界（PR 不得讓 ubuntu 的測試數下降）"

# GitHub Actions 的 `shell: bash` 預設展開式。**必須與實機一致**——
# 這幾個旗標正是 K-011 的成因，用別的旗標跑等於測了一個不存在的環境。
GH_BASH = ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail"]


def step_body():
    """取出 G3b 步驟的全部行。以**縮排**解析，不用 PyYAML。

    CI 的 `actions/setup-python@v5` 給的是乾淨的 Python，**沒有 PyYAML**；
    `test_g5_acc_identity.py` 初版就是因此在 CI 上全錯的。閘門的自我測試不該為了解析
    自己的 workflow 而給 CI 多一個依賴。
    """
    with io.open(WORKFLOW, encoding="utf-8") as fh:
        lines = fh.read().splitlines()
    start = indent = None
    for i, ln in enumerate(lines):
        if ln.strip() == "- name: " + STEP_NAME:
            start, indent = i, len(ln) - len(ln.lstrip())
            break
    assert start is not None, f"governance.yml 找不到步驟「{STEP_NAME}」"
    end = len(lines)
    for i in range(start + 1, len(lines)):
        ln = lines[i]
        if not ln.strip() or ln.lstrip().startswith("#"):
            continue
        if len(ln) - len(ln.lstrip()) <= indent and ln.strip().startswith("- "):
            end = i
            break
    return lines[start:end]


def run_script():
    """G3b 的 `run: |` 腳本原文。"""
    body = step_body()
    assert any(ln.strip() == "shell: bash" for ln in body), \
        f"{STEP_NAME} 的 shell 不再是 bash，本檔的 GH_BASH 假設已失效"
    at = next((i for i, ln in enumerate(body)
               if re.match(r"run:\s*[|>]", ln.strip())), None)
    assert at is not None, f"{STEP_NAME} 沒有 `run: |` 區塊"
    run_indent = len(body[at]) - len(body[at].lstrip())
    script = [ln for ln in body[at + 1:]
              if not ln.strip() or (len(ln) - len(ln.lstrip())) > run_indent]
    pad = min((len(ln) - len(ln.lstrip()) for ln in script if ln.strip()), default=0)
    return "\n".join(ln[pad:] if ln.strip() else "" for ln in script)


def decision():
    """腳本中的**判定**部分：自 `echo "註冊測試數：` 起到區塊結尾。

    前面那幾行（`git worktree add` / `cmake` / 兩個 `ctest -N` 賦值）是**取值**，
    在單元測試裡重跑需要一整個 CMake configure；本檔改以注入 `BASE_N` / `HEAD_N`
    取代取值，只驗判定。取值那幾行由下面的靜態測試守。
    """
    out, started = [], False
    for ln in run_script().splitlines():
        if "註冊測試數：" in ln and ln.strip().startswith("echo"):
            started = True
        if started:
            out.append(ln)
    assert len(out) >= 5, f"抓不到判定區塊，實得 {out}"
    return "\n".join(out)


def run_decision(base, head, body=""):
    """以 GitHub 的 shell 旗標執行判定，回傳 (exit_code, 合併輸出)。"""
    proc = subprocess.run(
        GH_BASH + ["-c", decision()],
        capture_output=True, text=True,
        encoding="utf-8", errors="replace",          # K-009：明示編碼
        stdin=subprocess.DEVNULL,
        env={**os.environ, "BASE_N": base, "HEAD_N": head, "PR_BODY": body},
    )
    return proc.returncode, proc.stdout + proc.stderr


class TestTheFloorActuallyBlocksADrop(unittest.TestCase):
    """本步驟存在的理由：測試數下降必須擋下，而且說得出數字。"""

    def test_fewer_registered_tests_than_base_is_rejected(self):
        code, out = run_decision("183", "181", "一般的 PR 內文\nCHG-20261112-07")
        self.assertNotEqual(
            code, 0,
            "ubuntu 註冊測試數下降卻被放過——那正是 H1-06 驗收時實測出三道防線全綠的那個洞")
        self.assertIn("183", out)
        self.assertIn("181", out)
        self.assertIn("::error::", out, f"擋下了卻沒印出原因（K-011）。實得：{out!r}")

    def test_more_tests_passes(self):
        code, out = run_decision("183", "184")
        self.assertEqual(code, 0, f"新增測試被擋下了。實得：{out!r}")

    def test_same_count_passes(self):
        code, out = run_decision("183", "183")
        self.assertEqual(code, 0, f"數量不變被擋下了。實得：{out!r}")


class TestTheExemptionIsExplicitAndAnchored(unittest.TestCase):
    """空轉方式 3：豁免標記若沒錨在行首，散文裡提到它就會開後門。"""

    def test_marker_at_line_start_grants_the_exemption(self):
        code, out = run_decision("183", "181",
                                 "ALLOW-TEST-COUNT-DROP: 合併兩支重複測試\n其他內文")
        self.assertEqual(code, 0, f"明示豁免仍被擋下。實得：{out!r}")
        self.assertIn("::warning::", out,
                      "豁免放行了但沒有任何可見訊息——那會變成靜默的例外")

    def test_marker_mentioned_mid_sentence_does_not_grant_it(self):
        code, out = run_decision("183", "181", "若要豁免請加 ALLOW-TEST-COUNT-DROP: 這一行")
        self.assertNotEqual(
            code, 0,
            "散文裡提到標記就開了後門——`^` 錨點不見了")

    def test_empty_body_does_not_grant_it(self):
        code, _ = run_decision("183", "181", "")
        self.assertNotEqual(code, 0)


class TestMissingNumbersStayFailClosed(unittest.TestCase):
    """空轉方式 2：`|| true` 的代價是空字串，而空字串不可以被當成放行。"""

    def test_empty_head_count_is_rejected(self):
        code, out = run_decision("183", "")
        self.assertNotEqual(code, 0, "取不到 head 的註冊數卻放行了（fail-open）")
        self.assertIn("fail-closed", out)

    def test_empty_base_count_is_rejected(self):
        code, out = run_decision("", "183")
        self.assertNotEqual(code, 0, "取不到 base 的註冊數卻放行了（fail-open）")
        self.assertIn("fail-closed", out)

    def test_non_numeric_count_is_rejected_rather_than_compared(self):
        code, out = run_decision("183", "18x")
        self.assertNotEqual(
            code, 0,
            "非數字沒被擋下——`[ x -lt y ]` 在非數字下回 2，而 `if` 會把它當成 false 放行")
        self.assertIn("fail-closed", out)


class TestValueFetchingIsGuarded(unittest.TestCase):
    """空轉方式 1 與 4：取值那幾行不在判定測試的覆蓋內，故以靜態測試守。"""

    def test_every_pipeline_assignment_has_a_guard(self):
        unguarded = [
            ln.strip() for ln in run_script().splitlines()
            if "=$(" in ln and "|" in ln.split("=$(", 1)[1] and "|| true" not in ln
        ]
        self.assertEqual(
            unguarded, [],
            "這些賦值含管線但沒有 `|| true`：pipefail + -e 會讓它們靜默中止，"
            f"下面的 ::error:: 永遠印不出來（K-011）。{unguarded}")

    def test_pr_body_comes_from_the_environment_not_interpolation(self):
        script = run_script()
        self.assertNotIn(
            "${{", script,
            "PR 內文被內插進 run: 區塊——那是命令注入（K-008，2026-08-10 真的發生過）")
        self.assertIn('"$PR_BODY"', script,
                      "PR 內文不是經 $PR_BODY 取用；請保持 env 取值的寫法")

    def test_base_count_is_taken_from_the_base_ref(self):
        script = run_script()
        self.assertIn("$BASE_REF", script,
                      "base 的註冊數不是取自 base 分支，下界就失去意義")


if __name__ == "__main__":
    unittest.main(verbosity=2)
