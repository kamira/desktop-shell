#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`G3c 單元必須在 ubuntu 留下可見的測試` 的行為測試。

**本檔把 `.github/workflows/governance.yml` 裡 G3c 步驟的 `run:` 腳本原封不動抽出來執行**，
並在 PATH 上放一支假 `ctest` 供它呼叫。這樣受測的不只是判斷式，連
`sed -n 's/^ *Test *#[0-9]*: *//p'` 那段名稱抽取也在覆蓋內——它抽錯的話，
整道閘門會對每個單元都說「0 個同名測試」，那是會誤殺一切的失效模式。

**G3c 的由來**：`G3b` 擋「測試數下降」，擋不住「新單元一上線就整份躲在 `if(WIN32)` 裡」
——數字沒降、只是沒升，於是 G3（>0）、G3b（不降）、sanitizer 一致性（兩個 ubuntu
建置一起看不到它，故相等）三道全綠，而該單元在 ubuntu 的覆蓋從來不存在。

**這不是假想**：實測本 repo 已合併的 `H1-05` 正是如此（`units.json` 標 `platform: neutral`，
而 `host/position/CMakeLists.txt` 把 `h1_05_edge_snap` 包在 `if(WIN32)` 內，
該檔註解還寫著「純幾何……同樣不碰 Windows API」）。

守的是 G3c 可能空轉或誤殺的六個方式：

1. **一律要求同名測試** → 誤殺平台專屬單元。實測 194 個已合併單元中有 13 個會中槍，
   而下一個待派的 `W1-07` 正屬該族。故以 `units.json` 的 `platform` 分流。
2. **未知的 platform 值靜默放行** → 閘門的生效與否交給 `units.json` 的手誤決定。必須 fail-closed。
3. **豁免標記沒有錨在行首** → PR 內文散文裡提到它就意外開了後門（同 G3b 已實測過的洞）。
4. **PR 內文被內插進腳本** → `K-008`（2026-08-10 真的發生過）。
5. **管線賦值少了 `|| true`** → `pipefail` + `-e` 下賦值失敗當場中止，`::error::` 印不出來（`K-011`）。
   本步驟還多一個：`grep -c` 在**零命中時回 1**，而零命中正是本閘門要處理的那一種。
6. **`sed` 抽不出名稱** → 永遠 0 命中，對每個單元都紅。以假 `ctest` 餵真實格式覆蓋。

可直接執行：`python3 tests/gates/test_unit_test_visible_on_ubuntu.py`
"""
import io
import os
import re
import shutil
import stat
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

STEP_NAME = "G3c 單元必須在 ubuntu 留下可見的測試"

# GitHub Actions 的 `shell: bash` 預設展開式。**必須與實機一致**——
# 這幾個旗標正是 K-011 的成因，用別的旗標跑等於測了一個不存在的環境。
GH_BASH = ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail"]

# 假 ctest 吐出的格式刻意照抄真實輸出（含前導空白與 `Test #N:`），
# 因為腳本裡的 sed 就是衝著這個格式寫的。
CTEST_N_TEMPLATE = """Test project /home/runner/work/desktop-shell/build
{rows}

Total Tests: {n}
"""


def step_body():
    """取出 G3c 步驟的全部行。以**縮排**解析，不用 PyYAML。

    CI 的 `actions/setup-python@v5` 給的是乾淨的 Python，**沒有 PyYAML**；
    `test_g5_acc_identity.py` 初版就是因此在 CI 上全錯的。閘門的自我測試不該為了
    解析自己的 workflow 而給 CI 多一個依賴。
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
    """G3c 的 `run: |` 腳本原文。"""
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


class Harness:
    """在 PATH 最前面放一支假 `ctest`，讓真腳本照常呼叫它。"""

    def __init__(self, test_names=None, ctest_rc=0):
        self.test_names = test_names
        self.ctest_rc = ctest_rc

    def __enter__(self):
        self.dir = tempfile.mkdtemp(prefix="g3c-")
        if self.test_names is None:
            payload = ""                       # 取不到任何東西（災難情境）
        else:
            rows = "\n".join(
                "  Test #%d: %s" % (i + 1, n) for i, n in enumerate(self.test_names))
            payload = CTEST_N_TEMPLATE.format(rows=rows, n=len(self.test_names))
        p = os.path.join(self.dir, "ctest")
        with io.open(p, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("#!/usr/bin/env bash\n")
            fh.write("cat <<'EOF_FAKE'\n%sEOF_FAKE\n" % payload)
            fh.write("exit %d\n" % self.ctest_rc)
        os.chmod(p, os.stat(p).st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)
        return self.dir

    def __exit__(self, *exc):
        shutil.rmtree(self.dir, ignore_errors=True)
        return False


def run_gate(platform="neutral", head_ref="feat/h1_08", body="",
             test_names=("h1_08_gauge", "e9_11_perm"), unit_id="H1-08",
             ctest_rc=0):
    """以 GitHub 的 shell 旗標執行 G3c，回傳 (exit_code, 合併輸出)。"""
    names = None if test_names is None else list(test_names)
    with Harness(names, ctest_rc) as fake_bin:
        env = {
            **os.environ,
            "PATH": fake_bin + os.pathsep + os.environ.get("PATH", ""),
            "HEAD_REF": head_ref,
            "PLATFORM": platform,
            "UNIT_ID": unit_id,
            "PR_BODY": body,
        }
        proc = subprocess.run(
            GH_BASH + ["-c", run_script()],
            capture_output=True, text=True,
            encoding="utf-8", errors="replace",      # K-009：明示編碼
            stdin=subprocess.DEVNULL,
            cwd=REPO_ROOT, env=env,
        )
    return proc.returncode, proc.stdout + proc.stderr


class TestTheHoleIsActuallyClosed(unittest.TestCase):
    """本步驟存在的理由：neutral 單元在 ubuntu 看不見時必須紅，而且說得出是哪一個。"""

    def test_neutral_unit_with_no_matching_test_is_rejected(self):
        code, out = run_gate(platform="neutral", head_ref="feat/h1_08",
                             test_names=("e9_11_perm", "c2_11_bus"))
        self.assertNotEqual(
            code, 0,
            "neutral 單元在 ubuntu 沒有任何同名測試卻放行了——"
            "那正是 H1-05 現況（neutral 卻整份在 if(WIN32) 內）的那個洞")
        self.assertIn("::error::", out, f"擋下了卻沒印出原因（K-011）。實得：{out!r}")
        self.assertIn("h1_08", out, "錯誤訊息沒說是哪一個 slug 找不到")

    def test_neutral_unit_with_a_matching_test_passes(self):
        code, out = run_gate(platform="neutral", head_ref="feat/h1_08",
                             test_names=("h1_08_gauge",))
        self.assertEqual(code, 0, f"有同名測試卻被擋下。實得：{out!r}")

    def test_substring_match_counts_the_suffixed_names(self):
        """實際命名是 `<slug>_<描述>`（h1_07_compose / h1_07_pixel_boundary），
        所以比對必須是子字串而非相等，否則每個單元都會紅。"""
        code, out = run_gate(platform="neutral", head_ref="feat/h1_07",
                             test_names=("h1_07_compose", "h1_07_pixel_boundary"))
        self.assertEqual(code, 0, f"後綴式命名沒被認出。實得：{out!r}")
        self.assertIn("2", out, "命中數沒有印出來，無法在 log 裡複核")

    def test_full_ref_form_of_the_branch_also_works(self):
        code, out = run_gate(platform="neutral",
                             head_ref="refs/heads/feat/h1_08",
                             test_names=("h1_08_gauge",))
        self.assertEqual(code, 0, f"refs/heads/ 形式的分支名沒被正確去前綴。實得：{out!r}")


class TestPlatformSpecificUnitsAreNotFalselyBlocked(unittest.TestCase):
    """空轉方式 1 的反面：這道閘門不可以把平台專屬單元擋死。

    實測 194 個已合併單元：一律要求 ubuntu 同名測試會誤殺 13 個
    （W1-01…W1-06、H1-01…H1-04 等），而下一個待派的 W1-07 正屬該族。
    """

    def test_semantic_divergence_unit_is_skipped(self):
        code, out = run_gate(platform="semantic-divergence",
                             head_ref="feat/w1_07", unit_id="W1-07",
                             test_names=("h1_08_gauge",))
        self.assertEqual(code, 0, f"平台專屬單元被誤殺。實得：{out!r}")
        self.assertIn("::notice::", out,
                      "跳過了卻沒留下任何訊息——那會變成看不見的例外")

    def test_api_divergence_unit_is_skipped(self):
        code, out = run_gate(platform="api-divergence",
                             head_ref="feat/h1_01", unit_id="H1-01",
                             test_names=("e9_11_perm",))
        self.assertEqual(code, 0, f"平台專屬單元被誤殺。實得：{out!r}")


class TestUnknownPlatformFailsClosed(unittest.TestCase):
    """空轉方式 2：未知值若當成「不是 neutral」而跳過，units.json 的手誤就能關掉這道閘門。"""

    def test_unrecognised_platform_is_rejected(self):
        code, out = run_gate(platform="windows-only", head_ref="feat/h1_08")
        self.assertNotEqual(code, 0, "無法辨識的 platform 值被靜默跳過了（fail-open）")
        self.assertIn("fail-closed", out)

    def test_empty_platform_is_rejected(self):
        code, out = run_gate(platform="", head_ref="feat/h1_08")
        self.assertNotEqual(code, 0, "platform 為空卻放行了（fail-open）")
        self.assertIn("fail-closed", out)


class TestMissingInputsStayFailClosed(unittest.TestCase):
    """空轉方式 5 / 6：取不到輸入時不可以變成放行。"""

    def test_empty_branch_ref_is_rejected(self):
        code, out = run_gate(head_ref="")
        self.assertNotEqual(code, 0, "取不到 slug 卻放行了")
        self.assertIn("fail-closed", out)

    def test_no_registered_tests_at_all_is_rejected(self):
        code, out = run_gate(test_names=None)
        self.assertNotEqual(code, 0, "一個註冊測試名稱都取不到卻放行了")
        self.assertIn("fail-closed", out)

    def test_zero_hits_does_not_abort_the_script_before_the_error(self):
        """`grep -c` 在零命中時回 1。少了 `|| true`，`-e` 會在賦值當場中止，
        於是 ::error:: 永遠印不出來——而零命中正是本閘門要處理的那一種。"""
        code, out = run_gate(platform="neutral", head_ref="feat/h1_08",
                             test_names=("e9_11_perm",))
        self.assertNotEqual(code, 0)
        self.assertIn("::error::", out,
                      "零命中時腳本提前中止了，只留下一個裸的 exit code（K-011）")


class TestTheExemptionIsExplicitAndAnchored(unittest.TestCase):
    """空轉方式 3：豁免標記若沒錨在行首，散文裡提到它就會開後門。"""

    def test_marker_at_line_start_grants_the_exemption(self):
        code, out = run_gate(platform="neutral", head_ref="feat/e1_24",
                             unit_id="E1-24", test_names=("e1_25_contract",),
                             body="ALLOW-NO-UBUNTU-TEST: 覆蓋由 E1-25 契約測試組提供\n其他內文")
        self.assertEqual(code, 0, f"明示豁免仍被擋下。實得：{out!r}")
        self.assertIn("::warning::", out,
                      "豁免放行了但沒有任何可見訊息——那會變成靜默的例外")

    def test_marker_mentioned_mid_sentence_does_not_grant_it(self):
        code, _ = run_gate(platform="neutral", head_ref="feat/h1_08",
                           test_names=("e9_11_perm",),
                           body="若要豁免請加 ALLOW-NO-UBUNTU-TEST: 這一行")
        self.assertNotEqual(code, 0, "散文裡提到標記就開了後門——`^` 錨點不見了")

    def test_empty_body_does_not_grant_it(self):
        code, _ = run_gate(platform="neutral", head_ref="feat/h1_08",
                           test_names=("e9_11_perm",), body="")
        self.assertNotEqual(code, 0)

    def test_the_other_gates_marker_does_not_grant_this_one(self):
        """G3b 的豁免標記不可以順便豁免 G3c——兩道閘門擋的是不同的事。"""
        code, _ = run_gate(platform="neutral", head_ref="feat/h1_08",
                           test_names=("e9_11_perm",),
                           body="ALLOW-TEST-COUNT-DROP: 合併兩支重複測試")
        self.assertNotEqual(code, 0, "G3b 的標記把 G3c 一起豁免掉了")


class TestStaticGuards(unittest.TestCase):
    """空轉方式 4 / 5：不在執行覆蓋內的部分，以靜態測試守。"""

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
            "不可信輸入被內插進 run: 區塊——那是命令注入（K-008，2026-08-10 真的發生過）")
        self.assertIn('"$PR_BODY"', script,
                      "PR 內文不是經 $PR_BODY 取用；請保持 env 取值的寫法")

    def test_the_step_only_runs_for_work_units(self):
        body = "\n".join(step_body())
        self.assertIn(
            "steps.unit.outputs.is_unit == 'true'", body,
            "G3c 沒有限定只對 feat/ 工作單元生效——infra 分支沒有 slug，會被誤殺")

    def test_platform_comes_from_the_unit_step(self):
        body = "\n".join(step_body())
        self.assertIn(
            "steps.unit.outputs.platform", body,
            "platform 不是取自 unit 步驟；寫死或改用別的來源會讓分流失準")


if __name__ == "__main__":
    unittest.main(verbosity=2)
