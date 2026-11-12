#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`gate_sanitizers` 閘門的行為測試。

**本檔不測「我寫的某個函式」，而是把 `.github/workflows/governance.yml` 裡
`gate_sanitizers` 步驟的 `run:` 腳本原封不動抽出來執行。** 理由與
`tests/gates/test_g5_acc_identity.py` 相同、該檔的 docstring 已寫明：那是一段內嵌在
workflow 裡的 shell，沒有任何 Python 入口，所以在本檔出現之前**它的判定邏輯無法在測試裡
被驗證**。而一道沒有自我測試的防線，正是 `K-011` 整條記載的那件事。

抽出來跑而不是在測試裡複製一份，是為了讓「有人改了 workflow 卻沒改測試」必然紅燈；
複製一份只會讓兩邊各自漂移，而測試仍然綠。

守的是這道閘門可能**空轉**的四個方式：

1. **UBSan 預設只印不中止。** 少了 `-fno-sanitize-recover=all`，UBSan 印出
   `runtime error:` 之後程序照樣回 0 → ctest 全綠。那就是「報告印出來了而閘門說通過」，
   與 `K-011`（擋對了卻印不出原因）互為鏡像：這次是印出來了卻沒擋。
2. **註冊數只檢查「大於 0」。** sanitizer 旗標若讓某個單元的 CMake 條件走進別的分支，
   該單元會被**靜默排除**，而其餘一百多個測試照樣印綠燈——本 repo 已防過兩次的假綠燈。
   故必須與同平台普通建置的註冊數**相等**，不是「至少一個」。
3. **管線賦值少了 `|| true`。** `bash -e -o pipefail` 下賦值失敗會當場中止，
   下一行的 `::error::` 永遠印不出來，只留一個裸的 exit code（`K-011` 本體）。
4. **`gate` 不以 needs 相依於本 job。** 那樣本閘門就只是裝飾：紅燈時 `gate` 照跑、
   自動合併照執行。`gate_windows` 的註解已寫明這個 fail-closed 設計的理由。

可直接執行：`python3 tests/gates/test_sanitizer_gate_checks.py`
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

JOB_NAME = "gate_sanitizers"

# GitHub Actions 的 `shell: bash` 預設展開式。**必須與實機一致**——
# 這幾個旗標正是 K-011 的成因，用別的旗標跑等於測了一個不存在的環境。
GH_BASH = ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail"]


def _lines():
    with io.open(WORKFLOW, encoding="utf-8") as fh:
        return fh.read().splitlines()


def job_body(name):
    """取出某個 job 的全部行。以**縮排**解析，不用 PyYAML。

    CI 的 `actions/setup-python@v5` 給的是乾淨的 Python，**沒有 PyYAML**；
    `test_g5_acc_identity.py` 初版就是因此在 CI 上全錯的。閘門的自我測試不該為了解析
    自己的 workflow 而給 CI 多一個依賴，而 `scripts/workflow_lint.py` 也已以縮排解析
    同一批 `run:` 區塊並在 docstring 寫明「刻意不用 YAML 解析」。
    """
    lines = _lines()
    start = indent = None
    for i, ln in enumerate(lines):
        if ln.strip() == name + ":" and ln.startswith("  ") and not ln.startswith("   "):
            start, indent = i, len(ln) - len(ln.lstrip())
            break
    assert start is not None, f"governance.yml 找不到 job「{name}」"
    end = len(lines)
    for i in range(start + 1, len(lines)):
        ln = lines[i]
        if not ln.strip() or ln.lstrip().startswith("#"):
            continue
        if (len(ln) - len(ln.lstrip())) <= indent:
            end = i
            break
    return lines[start:end]


def run_script():
    """取出 `gate_sanitizers` 唯一的 `run: |` 腳本原文。"""
    body = job_body(JOB_NAME)
    assert any(ln.strip() == "shell: bash" for ln in body), \
        f"{JOB_NAME} 的 shell 不再是 bash，本檔的 GH_BASH 假設已失效"
    at = next((i for i, ln in enumerate(body)
               if re.match(r"run:\s*[|>]", ln.strip())), None)
    assert at is not None, f"{JOB_NAME} 沒有 `run: |` 區塊"
    run_indent = len(body[at]) - len(body[at].lstrip())
    script = [ln for ln in body[at + 1:]
              if not ln.strip() or (len(ln) - len(ln.lstrip())) > run_indent]
    pad = min((len(ln) - len(ln.lstrip()) for ln in script if ln.strip()), default=0)
    return "\n".join(ln[pad:] if ln.strip() else "" for ln in script)


def registration_checks():
    """腳本中「註冊數比對」那幾行（echo + 三個 `[ … ]` 守衛）。

    只取這幾行是刻意的：整段腳本會做一次 sanitizer 全建（CI 上數分鐘），
    在單元測試裡重跑它既無必要也不可行。這幾行才是**判定**，而判定是本檔要守的東西。
    """
    out, started = [], False
    for ln in run_script().splitlines():
        s = ln.strip()
        if "註冊測試數：" in s:
            started = True
        if started:
            if not s.startswith(("echo", "[")):
                break
            out.append(s)
    assert len(out) >= 4, f"抓不到註冊數比對那幾行，實得 {out}"
    return "\n".join(out)


def run_checks(expect, got):
    """以 GitHub 的 shell 旗標跑註冊數比對，回傳 (exit_code, 合併輸出)。"""
    proc = subprocess.run(
        GH_BASH + ["-c", registration_checks()],
        capture_output=True, text=True,
        encoding="utf-8", errors="replace",          # K-009：明示編碼
        env={**os.environ, "EXPECT": expect, "GOT": got},
    )
    return proc.returncode, proc.stdout + proc.stderr


class TestRegistrationCountIsComparedNotJustCountedAboveZero(unittest.TestCase):
    """空轉方式 2：只檢查「大於 0」會讓被靜默排除的單元整批溜過去。"""

    def test_fewer_tests_than_the_plain_build_is_rejected(self):
        code, out = run_checks("181", "180")
        self.assertNotEqual(
            code, 0,
            "sanitizer 建置少了一個測試卻被放過——那就是靜默排除（假綠燈）")
        self.assertIn("有單元被靜默排除", out,
                      f"擋下了卻沒印出原因（K-011 同族）。實得：{out!r}")

    def test_matching_counts_pass(self):
        code, out = run_checks("181", "181")
        self.assertEqual(code, 0, f"數量相符卻被擋下。實得：{out!r}")

    def test_zero_tests_is_rejected_with_its_reason(self):
        code, out = run_checks("181", "0")
        self.assertNotEqual(code, 0)
        self.assertIn("綠燈無意義", out)


class TestMissingCountsStayFailClosed(unittest.TestCase):
    """空轉方式 3 的下游：取值失敗必須 fail-closed，而且說得出為什麼。

    `|| true` 讓賦值不中止，代價是值可能是空字串。若少了空值檢查，`[ "$GOT" = "$EXPECT" ]`
    在兩邊都空時會**相等**而放行——fail-closed 就變成 fail-open。
    """

    def test_empty_sanitizer_count_is_rejected(self):
        code, out = run_checks("181", "")
        self.assertNotEqual(code, 0, "取不到 sanitizer 註冊數卻放行了（fail-open）")
        self.assertIn("取不到註冊測試數", out)

    def test_both_counts_empty_is_rejected_rather_than_treated_as_equal(self):
        code, out = run_checks("", "")
        self.assertNotEqual(
            code, 0,
            "兩邊都取不到值時「相等」成立而被放過——這正是 fail-open 的形狀")
        self.assertIn("取不到註冊測試數", out)


class TestSanitizersActuallyAbort(unittest.TestCase):
    """空轉方式 1：UBSan 預設只印不中止，那會讓報告印出來而閘門說通過。"""

    def test_ubsan_is_not_recoverable(self):
        self.assertIn(
            "-fno-sanitize-recover=all", run_script(),
            "少了 -fno-sanitize-recover=all：UBSan 會印出 runtime error 後回 0，"
            "ctest 照樣全綠——報告有了，閘門卻沒擋")

    def test_leak_detection_is_explicit(self):
        self.assertIn("detect_leaks=1", run_script(),
                      "洩漏偵測未明示開啟；它在某些環境預設不中止")

    def test_address_and_undefined_are_both_enabled(self):
        script = run_script()
        for s in ("address", "undefined"):
            self.assertIn(s, script, f"-fsanitize 少了 {s}")


class TestEveryPipelineAssignmentIsGuarded(unittest.TestCase):
    """空轉方式 3：讓 K-011 不能靠「新增一行」悄悄回到這個 job 裡。"""

    def test_no_unguarded_command_substitution_with_a_pipe(self):
        unguarded = [
            ln.strip() for ln in run_script().splitlines()
            if "=$(" in ln and "|" in ln.split("=$(", 1)[1] and "|| true" not in ln
        ]
        self.assertEqual(
            unguarded, [],
            "這些賦值含管線但沒有 `|| true`：pipefail + -e 會讓它們靜默中止，"
            f"下一行的 ::error:: 永遠印不出來（K-011）。{unguarded}")


class TestTheGateIsFailClosedOnThisJob(unittest.TestCase):
    """空轉方式 4：`gate` 不相依於本 job，本閘門就只是裝飾。

    `gate_windows` 的註解已寫明這個設計：前置 job 紅燈 → `gate` 被跳過 →
    自動合併的指令**根本不會執行**。本閘門若不在 needs 裡，紅燈時仍會自動合併。
    """

    def test_gate_needs_the_sanitizer_job(self):
        needs = [ln.strip() for ln in job_body("gate") if ln.strip().startswith("needs:")]
        self.assertEqual(len(needs), 1, f"`gate` 的 needs 不止一行或不存在：{needs}")
        self.assertIn(
            JOB_NAME, needs[0],
            f"`gate` 未相依於 {JOB_NAME}：sanitizer 紅燈時自動合併仍會執行。實得：{needs[0]!r}")


class TestWerrorIsActuallyRequestedByTheBuildGate(unittest.TestCase):
    """旗標與本閘門互補，少了任一邊都有一整族錯誤無人看守。

    實測（加入當時）：把懸空參考形狀放回去，`-Wall -Wextra` **0 警告**而 ASan 抓到；
    把成員宣告順序對調，旗標抓到而 ASan **0 報告**。故 G3 必須真的開 `-Werror`。
    """

    def test_g3_builds_with_ds_werror_on(self):
        # **只看 configure 那一行，不看整個 job 本文。** 本檔初版寫成在 job 本文裡找
        # `-DDS_WERROR=ON`，而該字串也出現在 workflow 自己的註解裡——於是把真正的
        # cmake 參數拔掉之後測試仍然通過（突變 M5 存活）。那是 `K-012` 的形狀：
        # 一個永遠不會為正確理由失敗的檢查。註解行以 `#` 開頭，故按行首篩掉。
        cmakes = [ln.strip() for ln in job_body("gate")
                  if ln.strip().startswith("cmake -S . -B build")]
        self.assertEqual(len(cmakes), 1,
                         f"G3 的 configure 行不止一行或找不到：{cmakes}")
        self.assertIn(
            "-DDS_WERROR=ON", cmakes[0],
            "G3 的 configure 沒有 -DDS_WERROR=ON：警告不會擋紅，"
            f"於是 CHG 裡的「建置零警告」又回到幾近自動成立。實得：{cmakes[0]!r}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
