#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`G5b ACC 驗的必須是當前的程式碼` 的行為測試。

**本檔把 `.github/workflows/governance.yml` 裡 G5b 步驟的 `run:` 腳本原封不動抽出來，
在一個臨時的真 git 倉庫裡執行。** 不用假的 git：這道閘門的判定有一半是 git 語意
（祖先關係、兩點 diff 的檔案清單），只測字串處理等於沒測到會出事的那一半。

**G5b 的由來**：`G5` 檢查「ACC 存在、寫著 `Result: pass`、驗收者 ≠ 實作者」，
但**沒有**檢查那份 ACC 驗的是不是當前的 head。實測（`H1-09` / PR #244）：
獨立驗收判 pass 之後再推 256 行（含 47 行實作程式碼），G5 照樣綠燈、
G6 照樣貼「所有 CI 閘門已綠，等待人工核准」——人類看到的「HALT + 全綠」
因此證明不了 ACC 與程式碼是同一版。

守的是 G5b 可能空轉或誤殺的六個方式：

1. **跟 `HEAD` 比而不是跟 `head.sha` 比** → `pull_request` 的 checkout 是 merge ref，
   base 的變更會被算進來 → **誤殺**。一道會誤殺的閘門很快就會被繞過去。
2. **要求「宣告的 head == 當前 head」** → ACC 自己的提交就會改變 head，等式**永遠不成立**。
3. **ACC 沒寫受驗 head 時放行** → 整道閘門等於不存在。
4. **宣告一個不存在或不相干的 commit 時放行** → 同上，而且更難發現。
5. **`git diff` 失敗時放行** → 空清單被當成「什麼都沒動」（K-012 同族：fail-open）。
6. **管線賦值少了 `|| true`** → `pipefail` + `-e` 下當場中止，`::error::` 印不出來（K-011）。

可直接執行：`python3 tests/gates/test_acc_matches_head.py`
"""
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
WORKFLOW = os.path.join(REPO_ROOT, ".github/workflows/governance.yml")

STEP_NAME = "G5b ACC 驗的必須是當前的程式碼"
GH_BASH = ["bash", "--noprofile", "--norc", "-e", "-o", "pipefail"]


def step_body():
    """取出 G5b 步驟的全部行。以**縮排**解析，不用 PyYAML。

    CI 的 `actions/setup-python@v5` 給的是乾淨的 Python，**沒有 PyYAML**。
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


def git(cwd, *args):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", check=True).stdout.strip()


class Repo:
    """一個真的臨時 git 倉庫，模擬「實作 → 驗收寫 ACC → （可能再推程式碼）」。"""

    def __init__(self, acc_head_line=True, extra_code_after_acc=False,
                 extra_docs_after_acc=False, extra_chg_after_acc=False, slug="h1_09"):
        self.acc_head_line = acc_head_line
        self.extra_code = extra_code_after_acc
        self.extra_docs = extra_docs_after_acc
        self.extra_chg = extra_chg_after_acc
        self.slug = slug

    def __enter__(self):
        self.dir = tempfile.mkdtemp(prefix="g5b-")
        d = self.dir
        git(d, "init", "-q", "-b", "main")
        git(d, "config", "user.email", "t@example.invalid")
        git(d, "config", "user.name", "t")
        os.makedirs(os.path.join(d, "host/edit"), exist_ok=True)
        os.makedirs(os.path.join(d, "docs/acceptance"), exist_ok=True)
        os.makedirs(os.path.join(d, "docs/worklog"), exist_ok=True)

        self._write("host/edit/x.cpp", "int f(){return 1;}\n")
        git(d, "add", "-A"); git(d, "commit", "-qm", "impl")
        self.verified = git(d, "rev-parse", "HEAD")

        head_line = (f"- Date: 2026-11-17 | Unit: `X` | Verifier: V9 | Implemented by: I9\n"
                     f"- 受驗 head: `{self.verified}` | PR: #1\n" if self.acc_head_line
                     else "- Date: 2026-11-17 | Unit: `X` | Verifier: V9 | Implemented by: I9\n")
        self._write(f"docs/acceptance/ACC-20261117-{self.slug}.md",
                    head_line + "\nResult: pass\n")
        git(d, "add", "-A"); git(d, "commit", "-qm", "acc")

        if self.extra_docs:
            self._write("docs/worklog/V9-x.md", "note\n")
            git(d, "add", "-A"); git(d, "commit", "-qm", "worklog")
        if self.extra_chg:
            self._write(f"docs/changes/CHG-20261117-{self.slug}.md", "改過的 CHG\n")
            git(d, "add", "-A"); git(d, "commit", "-qm", "chg after acc")
        if self.extra_code:
            self._write("host/edit/x.cpp", "int f(){return 2;}\n")
            git(d, "add", "-A"); git(d, "commit", "-qm", "code after acc")

        self.head = git(d, "rev-parse", "HEAD")
        return self

    def _write(self, rel, text):
        p = os.path.join(self.dir, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with io.open(p, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)

    def __exit__(self, *exc):
        shutil.rmtree(self.dir, ignore_errors=True)
        return False


def run_gate(repo, head_sha=None, head_ref=None):
    env = {**os.environ,
           "GITHUB_HEAD_REF": head_ref if head_ref is not None else f"feat/{repo.slug}",
           "HEAD_SHA": repo.head if head_sha is None else head_sha}
    proc = subprocess.run(GH_BASH + ["-c", run_script()],
                          capture_output=True, text=True,
                          encoding="utf-8", errors="replace",
                          stdin=subprocess.DEVNULL, cwd=repo.dir, env=env)
    return proc.returncode, proc.stdout + proc.stderr


class TestTheHoleIsClosed(unittest.TestCase):
    """本步驟存在的理由：判 pass 之後再推程式碼，必須擋下。"""

    def test_code_pushed_after_the_acc_is_rejected(self):
        with Repo(extra_code_after_acc=True) as r:
            code, out = run_gate(r)
        self.assertNotEqual(code, 0,
                            "ACC 判 pass 之後又推了實作程式碼卻放行——那正是 H1-09 實測出的洞")
        self.assertIn("::error::", out, f"擋下了卻沒印出原因（K-011）。實得：{out!r}")
        self.assertIn("host/edit/x.cpp", out, "錯誤訊息沒說是哪個檔案被動過")

    def test_only_acceptance_and_worklog_changed_passes(self):
        """ACC 自己的提交會改變 head，那個等式永遠不成立——所以比的是檔案清單。"""
        with Repo(extra_docs_after_acc=True) as r:
            code, out = run_gate(r)
        self.assertEqual(code, 0, f"只動了 worklog 卻被擋下（誤殺）。實得：{out!r}")

    def test_chg_changed_after_the_acc_is_also_rejected(self):
        """`docs/changes/` **不在**放行清單裡，而且不該在：ACC 也驗 CHG
        （G5 就是從 CHG 取 `Implemented by:`），CHG 在驗收後被改，
        驗收者對它的陳述就不再成立。放寬成整個 `docs/` 會讓這條溜過去。"""
        with Repo(extra_chg_after_acc=True) as r:
            code, out = run_gate(r)
        self.assertNotEqual(code, 0, "ACC 判 pass 之後改了 CHG 卻放行")
        self.assertIn("docs/changes/", out, "錯誤訊息沒說是哪個檔案被動過")

    def test_acc_commit_itself_does_not_trip_the_gate(self):
        with Repo() as r:
            code, out = run_gate(r)
        self.assertEqual(code, 0, f"ACC 自己的提交把閘門絆倒了。實得：{out!r}")


class TestMissingOrBadDeclarationFailsClosed(unittest.TestCase):
    """空轉方式 3 / 4：宣告缺失或不相干時放行，等於整道閘門不存在。"""

    def test_acc_without_the_head_line_is_rejected(self):
        with Repo(acc_head_line=False) as r:
            code, out = run_gate(r)
        self.assertNotEqual(code, 0, "ACC 沒寫受驗 head 卻放行")
        # 釘住**那一句專屬訊息**，不是只釘「有擋下」：拿掉這個空值檢查之後，
        # 下游的 `git cat-file` 仍會擋，但訊息退化成
        # 「ACC 宣告的受驗 head␣␣不在本倉庫的歷史裡」——一個帶空白 sha 的爛訊息。
        # K-011 整條記的就是「擋對了卻說不出原因」，所以診斷品質本身要被釘住。
        self.assertIn("未寫明", out,
                      "擋下了，但不是由那個空值檢查擋的——訊息會退化成帶空白 sha 的版本")

    def test_unknown_commit_is_rejected(self):
        with Repo() as r:
            with io.open(os.path.join(r.dir, f"docs/acceptance/ACC-20261117-{r.slug}.md"),
                         "w", encoding="utf-8", newline="\n") as fh:
                fh.write("- 受驗 head: `deadbeefdeadbeefdeadbeefdeadbeefdeadbeef`\n\nResult: pass\n")
            code, out = run_gate(r)
        self.assertNotEqual(code, 0, "宣告了一個不存在的 commit 卻放行")

    def test_commit_from_another_line_of_history_is_rejected(self):
        """宣告的 commit 存在、但不是當前 head 的祖先 → 那份 ACC 驗的不是這條分支。"""
        with Repo() as r:
            git(r.dir, "checkout", "-q", "--orphan", "other")
            io.open(os.path.join(r.dir, "z.txt"), "w").write("z\n")
            git(r.dir, "add", "-A"); git(r.dir, "commit", "-qm", "orphan")
            other = git(r.dir, "rev-parse", "HEAD")
            git(r.dir, "checkout", "-q", "main")
            with io.open(os.path.join(r.dir, f"docs/acceptance/ACC-20261117-{r.slug}.md"),
                         "w", encoding="utf-8", newline="\n") as fh:
                fh.write(f"- 受驗 head: `{other}`\n\nResult: pass\n")
            code, out = run_gate(r)
        self.assertNotEqual(code, 0, "宣告了另一條歷史上的 commit 卻放行")
        self.assertIn("祖先", out)

    def test_empty_head_sha_is_rejected(self):
        with Repo() as r:
            code, out = run_gate(r, head_sha="")
        self.assertNotEqual(code, 0, "取不到 PR 的 head sha 卻放行（fail-open）")
        self.assertIn("fail-closed", out)


class TestStaticGuards(unittest.TestCase):
    """不在執行覆蓋內的部分，以靜態測試守。"""

    def test_compares_against_head_sha_not_HEAD(self):
        """空轉方式 1：`pull_request` 的 checkout 是 merge ref，
        拿 `HEAD` 比會把 base 的變更算進來 → 誤殺。"""
        script = run_script()
        self.assertIn('"$HEAD_SHA"', script,
                      "沒有跟 PR 的 head.sha 比對；拿 HEAD 比會誤殺（merge ref 含 base 變更）")
        self.assertNotIn("..HEAD", script,
                         "出現了與 HEAD 的比較——那會把 base 分支的變更算成本 PR 動過的檔案")

    def test_git_diff_failure_is_not_treated_as_no_change(self):
        """空轉方式 5：`git diff` 失敗若回空清單，會被當成「什麼都沒動」而放行。"""
        line = [ln for ln in run_script().splitlines() if "git diff --name-only" in ln]
        self.assertEqual(len(line), 1, f"預期剛好一行 git diff，實得 {line}")
        self.assertNotIn("|| true", line[0],
                         "git diff 加了 `|| true`：失敗會變成空清單而放行（fail-open）")
        self.assertIn("exit 1", line[0], "git diff 失敗沒有 fail-closed")

    def test_pipeline_assignments_are_guarded(self):
        """空轉方式 6：`ls` 查無檔案回 2、`grep` 無命中回 1，在 pipefail + -e 下會中止（K-011）。"""
        unguarded = [
            ln.strip() for ln in run_script().splitlines()
            if "=$(" in ln and "|" in ln.split("=$(", 1)[1]
            and "|| true" not in ln and "git diff" not in ln
        ]
        self.assertEqual(unguarded, [],
                         f"這些含管線的賦值沒有 `|| true`，::error:: 會印不出來（K-011）：{unguarded}")

    def test_only_runs_for_medium_or_above_work_units(self):
        body = "\n".join(step_body())
        self.assertIn("steps.unit.outputs.is_unit == 'true'", body,
                      "G5b 沒限定只對 feat/ 工作單元生效")
        self.assertIn("steps.unit.outputs.risk != 'low'", body,
                      "G5b 沒限定只對 medium 以上生效——low 風險沒有 ACC，會被誤殺")


if __name__ == "__main__":
    unittest.main(verbosity=2)
