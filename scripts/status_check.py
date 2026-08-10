#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""G7 閘門：本 PR 牽涉的 CHG，Status 必須已收尾才准合併。

## 為什麼需要它

漂移的機制是：合併發生在 PR 端，而 CHG 文件在合併之前就凍結在功能分支上。
整條流程（寫 CHG → 稽核 → PR → 八道閘門 → squash merge）**沒有任何一步會回頭改
Status**。2026-08-10 的進場握手掃出 103 筆停在 `Proposed` 或根本沒有 `## Status` 段的
已合併變更（`CHG-20260810-01` / `-02` 清了存量）。

Status 是進場握手判斷「有沒有半途而廢的階段」的唯一依據，而提醒 hook 只印前 5 筆。
兩者相乘的效果是**警告永遠不會歸零**，於是它會被當成背景雜訊——下一次真的有未收尾時，
沒有人會停下來看。

## 為什麼是「合併前擋下」而不是「合併後回寫」

合併後由 CI 自動蓋 `Accepted` 看似省事，但 `is_unit=false` 的 infra 分支**不跑 G5**
（ACC 存在 + `Result: pass` + 驗收者≠實作者）。對那些 PR，「合併了」只代表建置與結構
閘門過了，不代表驗收過。機器在那裡蓋 `Accepted`，就是一個恆為真的判定——與沒有判定
是同一件事（同 `governance.yml` 已防兩次的「真空綠燈」）。

所以本閘門**只讀不寫**：判定仍由人做（作者本來就完成了驗證，只是忘了寫那一行），
閘門要求他寫，不代替他判。

## 判定規則

與 ai-sdlc 的 `session_start.py` **刻意逐字對齊**：取 `## Status`（或 `## 狀態`）段的
**第一行結論**，比對收尾詞。兩套判定若分歧，等於又多一個漂移來源。

- 收尾：`已驗收` / `已收尾` / `accepted` / `closed`
- 合法 WIP：`暫停` / `paused` —— 刻意停著的 CHG 不該被擋

找不到 `## Status` 段時退回全文比對（同 `session_start.py`）——實務上等同紅燈，
因為 CHG 正文極少出現這些詞；這是刻意的：98 筆漂移裡最多的一種寫法就是「根本沒有
Status 段」。

用法： status_check.py --base origin/main [--pr-body-file <檔>]
"""
import argparse
import glob
import os
import re
import subprocess
import sys

# K-001 / K-004 同族：不釘住輸出編碼，在非 UTF-8 主控台印 CJK 會 UnicodeEncodeError，
# 閘門會以「當掉」而非「判定」收場——而當掉與正確擋下在 exit code 上無法區分。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ACCEPTED = ("已驗收", "已收尾", "accepted", "closed")
PAUSED = ("暫停", "paused")
STATUS_RE = re.compile(r"^##\s*(?:狀態|Status)\b(.*?)(?=^##\s|\Z)", re.MULTILINE | re.DOTALL)
CHG_ID_RE = re.compile(r"CHG-\d{8}-[A-Za-z0-9_]+")


def status_scope(text):
    """取 Status 段的第一行結論；無 Status 段則退回全文（與 session_start.py 一致）。"""
    m = STATUS_RE.search(text)
    if not m:
        return text.lower()
    for line in m.group(1).splitlines():
        if line.strip():
            return line.strip().lower()
    return ""


def is_closed(text):
    scope = status_scope(text)
    return any(h in scope for h in ACCEPTED) or any(h in scope for h in PAUSED)


def has_status_section(text):
    return STATUS_RE.search(text) is not None


def changed_chg_files(base, root=ROOT):
    out = subprocess.run(
        ["git", "diff", "--name-only", "%s...HEAD" % base, "--", "docs/changes/"],
        cwd=root, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    return [p for p in out.split() if p.endswith(".md") and os.path.exists(os.path.join(root, p))]


def files_for_ids(ids, root=ROOT):
    """由 CHG id 反查檔案。檔名可能帶後綴（如 CHG-20260803-04-w1_01.md）。

    回傳 (檔案清單, 查無檔案的 id 清單)。查無者**不是**放行理由——見 check()。
    """
    found, missing = [], []
    for cid in ids:
        hits = sorted(glob.glob(os.path.join(root, "docs", "changes", cid + "*.md")))
        if hits:
            found += [os.path.relpath(h, root).replace(os.sep, "/") for h in hits]
        else:
            missing.append(cid)
    return found, missing


def check(base, pr_body="", root=ROOT):
    """回傳 exit code。0=綠，1=擋下。訊息一律印到 stdout。"""
    files = changed_chg_files(base, root)
    source = "本 PR 異動的 CHG"

    if not files:
        # PR 沒動任何 CHG 檔（引用既有 CHG 的情形）：退回讀 PR 內文。
        # G2 已保證內文含 CHG 參照，所以這裡拿得到 id。
        ids = sorted(set(CHG_ID_RE.findall(pr_body or "")))
        files, missing = files_for_ids(ids, root)
        source = "PR 內文參照的 CHG"
        if missing:
            # fail-closed：內文寫了一個 repo 裡不存在的 CHG id，不能當作沒事。
            print("::error::PR 內文參照的 CHG 在 repo 中不存在：%s" % "、".join(missing))
            return 1
        if not files:
            print("::error::找不到任何可檢查的 CHG——本 PR 未異動 docs/changes/，"
                  "PR 內文也沒有可解析的 CHG id。G7 fail-closed，不放行。")
            return 1

    bad = []
    for rel in sorted(set(files)):
        with open(os.path.join(root, rel), encoding="utf-8", errors="ignore") as fh:
            text = fh.read()
        if is_closed(text):
            continue
        if not has_status_section(text):
            bad.append((rel, "完全沒有 `## Status` 段"))
        else:
            first = status_scope(text)
            bad.append((rel, "Status 首行為「%s」" % (first[:60] or "（空白）")))

    if bad:
        print("::error::以下 CHG 尚未收尾，不得合併（G7）：")
        for rel, why in bad:
            print("::error::  %s —— %s" % (rel, why))
        print("修法：把該 CHG 的 `## Status` 首行改成 `Accepted — …`（或 `Paused — <理由>` "
              "若是刻意擱置的 WIP），再推一次。")
        print("理由：Status 是進場握手判斷「有沒有半途而廢的階段」的唯一依據；"
              "讓 Proposed 進 main，下一個接手的人就得自己查 git log 才知道它到底做完沒有。")
        return 1

    print("status_check OK（%s，共 %d 份皆已收尾）" % (source, len(set(files))))
    for rel in sorted(set(files))[:10]:
        print("  ✓ %s" % rel)
    if len(set(files)) > 10:
        print("  …（其餘 %d 份省略）" % (len(set(files)) - 10))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--pr-body-file", default=None,
                    help="PR 內文檔案路徑；PR 未異動任何 CHG 檔時用來反查。")
    a = ap.parse_args()
    body = ""
    if a.pr_body_file and os.path.exists(a.pr_body_file):
        with open(a.pr_body_file, encoding="utf-8", errors="ignore") as fh:
            body = fh.read()
    sys.exit(check(a.base, body))


if __name__ == "__main__":
    main()
