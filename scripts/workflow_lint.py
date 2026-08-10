#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""G8 閘門：workflow 的 `run:` 區塊不得直接內插不可信的 `${{ }}` 運算式。

## 錯誤長什麼樣

GitHub Actions 的 `${{ }}` 是**在 shell 看到腳本之前**由 Actions 展開成字面文字的。
所以這一行：

    run: echo "${{ github.event.pull_request.body }}" | grep -qE 'CHG-'

送進 bash 的不是「一個變數」，而是**把 PR 內文原封不動貼進腳本裡**。
內文裡的反引號與 `$(...)` 於是變成命令替換——PR 作者寫什麼，runner 就執行什麼。

2026-08-10 這件事真的發生了（見 `docs/knowledge/errors.md` K-008）：一份 PR 內文
裡的 `` `scripts/status_check.py` `` 等 markdown 行內程式碼，在 G2 步驟被當成指令執行，
log 留下一整排 `command not found` / `Permission denied`。那次是良性的，
但同一條路徑上換成 `$(curl …|sh)` 就是拿到帶 `contents: write` 的 GITHUB_TOKEN。

## 正確寫法

透過 `env:` 傳，讓 shell 自己去讀變數（值永遠是資料，不會被當成腳本）：

    env:
      PR_BODY: ${{ github.event.pull_request.body }}
    run: printf '%s' "$PR_BODY" | grep -qE 'CHG-'

## 判定規則：允許清單，不是黑名單

黑名單擋不完（今天擋 `body`，明天有人用 `title`、`head_ref`、issue comment…）。
所以反過來：**`run:` 裡出現的 `${{ }}` 一律視為違規**，除非在允許清單上——
清單只放結構上不可能挾帶 shell 元字元的東西（數字、repo 全名、commit SHA）。

分支名值得特別說明：git 允許分支名含 `$`、`(`、`)`、反引號，
所以 `github.head_ref` / `github.base_ref` 內插進 `run:` 同樣是注入面，一併擋。

需要例外時，在該行或上一行加註 `# workflow-lint: allow <理由>`——
**理由不可空白**，空白的豁免等於沒有簽名。

用法： workflow_lint.py [--dir .github/workflows]
"""
import argparse
import os
import re
import sys

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

EXPR_RE = re.compile(r"\$\{\{\s*(.+?)\s*\}\}")
ALLOW_RE = re.compile(r"#\s*workflow-lint:\s*allow\s+(\S.*)$")

# 只放結構上不可能含 shell 元字元者。新增項目請附理由。
SAFE_EXPRESSIONS = {
    "github.event.pull_request.number",  # 整數
    "github.event.number",               # 整數
    "github.run_id",                     # 整數
    "github.run_number",                 # 整數
    "github.sha",                        # 40 位 hex
    "github.repository",                 # owner/repo，字元集受 GitHub 限制
    "github.repository_owner",
    "github.workspace",                  # runner 產生的路徑
}
# steps.<id>.outputs.<name>：值由本 repo 的腳本產生，不是事件酬載。
SAFE_PATTERNS = [re.compile(r"^steps\.[A-Za-z0-9_-]+\.outputs\.[A-Za-z0-9_-]+$")]


def is_safe(expr):
    if expr in SAFE_EXPRESSIONS:
        return True
    return any(p.match(expr) for p in SAFE_PATTERNS)


def run_block_lines(lines):
    """回傳位於 `run:` 區塊內的行號（1-based）集合。

    以縮排判斷區塊範圍：`run: |` 之後縮排更深的行都屬於它。
    刻意不用 YAML 解析——解析後拿不到行號，而閘門訊息沒有行號就沒人修得動。
    """
    inside = set()
    run_indent = None
    for i, raw in enumerate(lines, 1):
        line = raw.rstrip("\n")
        stripped = line.strip()
        if not stripped:
            continue
        indent = len(line) - len(line.lstrip())
        if run_indent is not None:
            if indent > run_indent:
                inside.add(i)
                continue
            run_indent = None
        m = re.match(r"-?\s*run:\s*(.*)$", stripped)
        if m:
            inside.add(i)                 # 單行 run: 形式
            if m.group(1).strip() in ("|", ">", "|-", ">-", "|+", ">+"):
                run_indent = indent
    return inside


def check_file(path, rel):
    with open(path, encoding="utf-8", errors="replace") as fh:
        lines = fh.readlines()
    inside = run_block_lines(lines)
    bad = []
    for i, raw in enumerate(lines, 1):
        if i not in inside:
            continue
        exprs = EXPR_RE.findall(raw)
        if not exprs:
            continue
        unsafe = [e for e in exprs if not is_safe(e)]
        if not unsafe:
            continue
        # 豁免：本行或上一行的 `# workflow-lint: allow <理由>`
        context = raw + (lines[i - 2] if i >= 2 else "")
        m = ALLOW_RE.search(context)
        if m and m.group(1).strip():
            continue
        for e in unsafe:
            bad.append((rel, i, e))
    return bad


def check(workflows_dir):
    if not os.path.isdir(workflows_dir):
        print("::error::找不到 workflow 目錄：%s" % workflows_dir)
        return 1
    files = sorted(f for f in os.listdir(workflows_dir)
                   if f.endswith((".yml", ".yaml")))
    if not files:
        # fail-closed：掃了 0 個檔卻回綠，與沒有閘門同義。
        print("::error::%s 之下沒有任何 workflow 檔，無從檢查（fail-closed）" % workflows_dir)
        return 1

    bad = []
    for name in files:
        bad += check_file(os.path.join(workflows_dir, name), name)

    if bad:
        print("::error::workflow 的 run: 區塊直接內插了不可信的 ${{ }}——這是命令注入：")
        for rel, line, expr in bad:
            print("::error::  %s:%d  ${{ %s }}" % (rel, line, expr))
        print("修法：改用 env: 傳值，讓 shell 自己讀變數——")
        print("  env:")
        print("    PR_BODY: ${{ github.event.pull_request.body }}")
        print("  run: printf '%s' \"$PR_BODY\" | grep -qE 'CHG-'")
        print("理由：${{ }} 是在 shell 看到腳本之前就展開成字面文字的，"
              "內文裡的反引號與 $(...) 會被當成命令執行（見 knowledge K-008）。")
        print("確有必要時加註 `# workflow-lint: allow <理由>`，理由不可空白。")
        return 1

    print("workflow_lint OK（%d 個檔，run: 區塊無不可信內插）" % len(files))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=os.path.join(ROOT, ".github", "workflows"))
    a = ap.parse_args()
    sys.exit(check(a.dir))


if __name__ == "__main__":
    main()
