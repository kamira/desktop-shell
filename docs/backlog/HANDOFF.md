# HANDOFF — ai-sdlc-autopilot 全實作進度交接

- Date: 2026-07-28（初版）/ 2026-07-30 更新 / **2026-08-03 更新** | 角色: I1（lead implementer / 協調者）
- 本檔為 I1 交接便箋，非治理產物。權威狀態一律以 `origin/main` 的 `docs/changes/CHG-*.md` 為準。

---

## 0-A. 最新狀態（2026-08-03，**本段最新，蓋過下方 §0 與 §1~2**）

### 一句話

**相位 2（Windows）已實質完成**：桌面上有一個可拖曳、會記位置、有托盤選單與無障礙支援的
真實 widget。176 → **187 個單元**、**187 個測試**、MSVC 全建 0 warning、
每個 PR 跑 ubuntu + Windows 雙閘門。

### 接手第一件事：讀知識庫

`docs/knowledge/INDEX.md` 有 **7 條** K 條目。**至少先讀 K-001、K-004、K-007**——
它們描述的是這個環境會反覆咬人的陷阱，不是歷史紀錄。

| 條目 | 一句話 |
|---|---|
| K-001 | MSVC 以系統 ANSI codepage 誤讀 UTF-8 原始碼 → 69 個編譯錯誤。已由根 CMake 的 `/utf-8` 解決 |
| K-002 | 用 `0.0` 當取樣失敗值 → 壞掉的指標偽裝成「很閒」，且騙過只查值域的驗收 |
| K-003 | 契約測試只測自帶 stub、守它的閘門掃錯副檔名 → 兩道防線同時空轉（已修） |
| K-004 | **PowerShell 讀 BOM-less `.ps1` 也用 ANSI codepage** —— 與 K-001 同族，寫腳本必踩 |
| K-005 | **squash merge 後從舊分支再開分支 → 必然衝突，且閘門會安靜地不跑** |
| K-006 | CI 釘死 VS 產生器版本 → 隨 runner 映像更新而壞；附一個「恰好符合預期但成因不同」的假線索 |
| K-007 | 契約一接上真實後端就抓到 16 個單元踩在不可能成立的路徑上（已對齊） |

### 相位 2 新增的 11 個單元

| 單元 | 內容 | CHG |
|---|---|---|
| W1-01 | win32 kernel 後端（視窗 / 具名圖層 / 輸入策略） | `-04` |
| W1-02 | win32 系統匣後端 + 原生右鍵選單 | `-06` |
| W1-03 | win32 視窗拖曳 + 位置讀寫 | `-09` |
| W1-04 | 系統匣真實圖示資源 | `-14` |
| W1-05 | win32 自繪選單呈現（E11-02 上線） | `-15` |
| W1-06 | 自繪選單無障礙支援（MSAA） | `-16` |
| H1-01 | host shell app（GDI 繪製 + 主迴圈 + 浮動 widget） | `-05` |
| H1-02 | host 托盤裝配 + widget 控制命令 | `-06` |
| H1-03 | host 拖曳裝配 + 位置持久化 | `-09` |
| H1-04 | 鎖定位置開關 + UI 狀態持久化 | `-12` |
| H1-05 | 邊緣吸附 | `-13` |

治理面另有：`-01` MSVC `/utf-8`、`-02` 驗證器取樣修正、`-03` 翻相位 + 契約閘門補洞、
`-07` K-005 收錄、`-08` **G3w windows runner**、`-10` 契約鴻溝修復、`-11` K-007 對齊。

### 執行

```bash
build/units/host/app/Debug/desktop_shell_host.exe
```

托盤右鍵有選單（最上層 / 點擊穿透 / 鎖定位置 / 結束）。widget 可拖曳、會吸邊、記位置。
設定檔在 `%LOCALAPPDATA%\desktop-shell\`（`positions.conf`、`ui-state.conf`）。

---

## 0-B. ⚠ 轉移到無防毒環境後**必須補做**的操作驗收

本機的防毒（PC-cillin）**持續封鎖 `desktop_shell_host.exe` 執行**
（`Access is denied`，重試 6 次 + 等待 90 秒仍然如此；檔案未被刪除）。
測試執行檔不受影響，故所有單元測試都跑得動；但**部分功能只有單元測試，沒有實機證據**。

**未繞過防毒。** 以下清單是轉移環境後的第一件事：

| # | 要驗什麼 | 為什麼單元測試不夠 | 相關 |
|---|---|---|---|
| 1 | **自繪選單的實機外觀** | 只驗過記憶體 DIB 的像素，沒看過真實視窗 | W1-05 |
| 2 | **選單的滑鼠 hover / 點選 / Esc / 點外面關閉** | modal 迴圈與輸入路由沒被真實輸入走過 | W1-05 |
| 3 | **選單彈出位置與邊緣翻轉的實機行為** | 幾何函式已驗，但實機的工作區 / DPI 未驗 | W1-05 |
| 4 | **螢幕閱讀器（NVDA / Narrator）實際朗讀** | MSAA 介面回報正確 ≠ 閱讀器體驗可用 | W1-06 |
| 5 | 托盤圖示在通知區的實際外觀 | 只驗過檔案部署與載入路徑 | W1-04 |
| 6 | 合成滑鼠拖曳為何在本機時好時壞 | 原因未查明；已改用 `WM_NCHITTEST` 確定性驗收 | W1-03 / H1-04 |

**驗收腳本**：`CHG-20260803-12` ~ `-15` 的 Verification 段落有各自的做法可循
（送 `WM_EXITSIZEMOVE` 觸發吸附、送 `WM_APP+1` 叫出托盤選單、`WM_NCHITTEST` 查可拖曳性）。

### 本機環境（供對照）

Python 3.12.10、CMake 4.4.1、MSVC 19.44.35228（VS Build Tools 2022 17.14）、
Windows SDK 10.0.26100、gh 2.97.0。全驗證：

```bash
cmake -S . -B build -A x64
cmake --build build --config Debug --parallel 4
ctest --test-dir build -C Debug
```

---

## 0-C. 未關閉的技術債

| 項目 | 說明 |
|---|---|
| **自繪選單子選單未展開** | 只畫右側箭頭，不展開子面板。托盤選單無子選單，不影響現況（W1-05） |
| **無障礙未實作 `accSelect` / `accDoDefaultAction`** | 閱讀器可朗讀與導覽，但無法代為點擊（W1-06） |
| **多螢幕吸附未驗** | `work_area()` 取的是主要顯示器工作區，副螢幕行為未知（H1-05） |
| **圖示無主題分版** | 只有一個 `tray.ico`，無淺色 / 深色版，無 DPI 分級（W1-04） |
| **吸附門檻為編譯期常數** | 16px 寫死，無 UI 開關、不隨 DPI 調整（H1-05） |
| **E9 套件內圖示載入管線未接** | W1-04 只對齊了命名慣例（`icons/<name>`），未接實際套件載入 |
| **契約 17 條不代表窮盡** | `create_surface` 的前置條件已對齊，其他方法是否也有後端分歧未知（K-007） |

---

## 0-D. 路線圖剩下的

依 `docs/backlog/phase2-desktop-runtime.md`：

- **WS4 — skin 套件格式**：定義 skin = E9 manifest + E7 宣告式設定檔 + 素材；
  把 `examples/cpu_gpu_validator` 改寫成第一個範本 skin（建議獨立 repo）
- **相位 3 — cocoa 後端**：`phase.json` 的 `phases["3"]` 已定義好白名單；
  契約測試只需再加一個註冊點（見 `docs/structure/units/e1_25.md`），契約標頭不動

---

## 0-E. 治理現況（與 2026-07-30 相比的變化）

- **相位已翻到 2**：`phase.json` `"phase": 2`、`allowed_backends: ["null","win32"]`
- **閘門從 8 道變成兩個 job**：`gate_windows`（MSVC 全建 + ctest）→ `gate`（原八道）。
  `gate` 以 `needs: gate_windows` 相依，**Windows 紅燈時 `gate` 被跳過、自動合併不會執行**
- **`backend_guard` 相位閘門已擴及 `src/` 下任何平台命名目錄**（原本只掃 `src/kernel/backend/*`）
- **`tests/contract/` 現在真的驗證真實後端**：一份契約標頭、兩個註冊點（null / win32），各 17 條
- **`state.json` 已重建**：`plan.py status` 不再顯示「已完成 0」的假象

---

## 0（舊）最新狀態（2026-07-30，已被 §0-A 蓋過）

**相位 1 已全部完成並收官。** 176/176 工作單元全數合併，另有 4 個 2026-07-30 infra/交付 CHG（皆本機 cmake + CI 雙驗證後合併）：

| CHG | 內容 |
|---|---|
| CHG-20260730-01 | governance 閘門支援 **infra/非-feat 分支跑 G3 建置**（`is_unit=false` 跳過 unit 專屬閘、仍跑 G1b/G1c/G2/G3/G4 + infra AUTO 合併）。之前 infra CHG 都盲 admin-merge，現在有真實建置驗證 |
| CHG-20260730-02 | 解除 `ds::kernel::HitResult` 上游命名碰撞：**E1-02 改名 `InputHitResult`**（E1-04 struct 不動）；移除 C1-03 薄橋接；C1-06/C2-07 較深橋接保留並更正註解。本機 ctest 175/175 |
| CHG-20260730-03 | **目錄扁平化**：去除全庫 per-unit slug 層（`modules/elements/e4_07/x.cpp`→`modules/elements/x.cpp`）；355 檔搬移、19 子系統合併 CMakeLists、root GLOB 深度減一。本機全建+ctest 175/175 |
| CHG-20260730-04 | **最終交付：CPU/GPU Usage Widget 驗證器**（`examples/cpu_gpu_validator/`）純組裝 C2-02+E2-01+E4-03+E7-01、不改 src/，跑 live ASCII 量表螢幕（CPU 讀真實負載）。另以主機端 Swift/AppKit 覆蓋層展示於 macOS 桌面最上層（scratchpad，未收進 repo）|

**本機環境**：已裝 cmake 4.4.1（`brew install cmake`）。全驗證 = `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j2 && ctest --test-dir build`（約 18 分）。

**下一個大工作 = 轉移到 Windows 開發（2026-07-31 使用者確認，已選 B 案）**：把擴充點變成 **Rainmeter 式桌面 runtime**（托盤選單、最上層切換、浮動⇄固定、點擊穿透、widget skin 設定檔）。完整 backlog + **Windows 接手 checklist** 見 **`docs/backlog/phase2-desktop-runtime.md`**（B 案 = 相位 2 win32，與 PHASE-PLAN 原定案一致）。核心結論：功能全是現成單元的組裝（E11-01 托盤 / E1-01 最上層 / E1-08 拖曳 / E1-02 穿透 / E9+E7 設定檔），**引擎不需新 repo**；缺的是 win32 真實後端 + host shell app（同 repo）；只有 skin 內容包適合獨立 repo。

**Windows 接手前置（CHG-20260731-01 已處理）**：全庫平台相依審計 → **176 核心單元全平台中立、MSVC 應可編**；唯一 POSIX 相依 `examples/cpu_gpu_validator`（getloadavg）**已改跨平台**（`#ifdef _WIN32` 走 GetSystemTimes）。**MSVC caveat**：176 單元從未在 MSVC 實編（以 clang/g++ 寫），Windows **day-1 先跑完整 MSVC 全建**，把 MSVC 嚴格度問題（缺 include、`NOMINMAX`、two-phase lookup）一次機械修掉，再開始 win32 後端。CI 現為 ubuntu（守平台中立性），需加 windows runner 才守得住 MSVC 編譯。相位翻轉（`phase.json` → `"phase":2`）為 Windows day-1 的治理動作，見 phase2 checklist。

---

## 1. 目前進度（※此段為 2026-07-28 舊快照，最新以 §0 為準）

- **已合併 82 / 176**（另有數個 PR 在途/剛回報未推，見第 2 節），剩餘約 94。
- 重算 done-set 指令（`docs/backlog/state.json` 僅本地便利追蹤，欄位 `done`(字串陣列)/`in_progress`(物件)，可隨時由此重建）：
  ```bash
  git fetch origin main -q
  python3 - <<'PY'
  import json,subprocess,re
  slugs=subprocess.run(["git","ls-tree","-r","--name-only","origin/main","docs/changes/"],capture_output=True,text=True).stdout
  merged=set(m.upper().replace('_','-') for m in re.findall(r'CHG-20260724-(e\d+_\d+)',slugs))
  U={u['id']:u for u in json.load(open('docs/backlog/units.json'))['units']}
  done=sorted(u for u in U if u in merged)
  json.dump({"done":done,"in_progress":{}},open('docs/backlog/state.json','w'),indent=1,ensure_ascii=False)
  d=set(done)
  ready=[u for u in U.values() if u['id'] not in d and all(x in d for x in u.get('depends_on',[]))]
  ready.sort(key=lambda u:(u.get('risk')!='low',-u.get('load_bearing',0),u.get('wave',9)))
  print(f"done={len(done)} 剩餘={len(U)-len(done)} ready={len(ready)}")
  for u in ready[:25]: print(' ',u['id'],u['risk'],u['layer'],u.get('subsystem') or '-',u['title'])
  PY
  ```
- 全部 5 個擴充點契約（E2-01 指標 / E4 繪製 / E6-01 命令匯流排 / E1 profile / E8-04+E8-01 腳本模組載入）已合併並驗證；**系統監控量測全套**（E2-03 CPU、E2-05 GPU、E2-04 記憶體、E2-06/07 儲存、E2-08 網路、E2-09 電源、E2-11 uptime、E2-15 音訊、E2-17 感測器、E2-18 效能計數器、E2-19 行程、E2-20 連通性、E2-24 Wi-Fi …）已合併——**最終 Widget 的資料源齊備**。

## 2. 在途工作（接手第一件事：先收掉這些）

- **PR #91**（E9-04 主題切換）：已開，等 CI 自動合併。
- **剛回報、尚未 push/PR** 的分支（跑第 4 節流程收掉）：
  - E8-01 腳本引擎 → `feat/e8_01` @ `410b38a`（35 gtest）
  - E7-14 圖形化設定就地編輯 → `feat/e7_14` @ `ffae41f`（16 gtest）
- **可能仍在跑**：E7-15 拖放產生設定項 → `feat/e7_15`（`agent-a7735e6f4696750ec`）。若已回報則一併收掉；worktree 收尾 `git worktree remove --force .claude/worktrees/agent-<id>` + `git worktree prune`。

## 3. 標準指令（使用者授權，持續有效）

- 「依流程進行 並且自動核准 CI一過就合併」— 所有單元（含 medium）CI 綠即自動合併，不需再逐一問。
- 需求/設計精修一律**延後到全部 task 結束後**再處理。
- 最終交付：**依使用環境建置 CPU/GPU Usage Widget 驗證器並測試**（功能測試：從擴充點組裝 widget、不改 src/）。

## 4. 每單元 audit→push→PR 流程（I1 手動）

```bash
# 1) scope 稽核（三點 diff 只含該單元 write_scope + COMMON）
python3 - <<'PY'
import json,fnmatch,subprocess
UID="E?-??"; SLUG="e?_??"
U={u['id']:u for u in json.load(open('docs/backlog/units.json'))['units']}
allow=U[UID]['write_scope']+["docs/worklog/**","docs/changes/**","docs/acceptance/**","docs/structure/**","docs/knowledge/errors.md"]
files=subprocess.run(["git","diff","--name-only",f"origin/main...feat/{SLUG}"],capture_output=True,text=True).stdout.split()
bad=[f for f in files if not any(fnmatch.fnmatch(f,p) or fnmatch.fnmatch(f,p.replace('/**','/*')) for p in allow)]
print(UID,len(files),"越權=",bad or "無✓","結構檔=",f"docs/structure/units/{SLUG}.md" in files)
PY
# 2) push + PR —— PR 內文【務必含】CHG-20260724-<slug> 參照（見第 5 節 G2 陷阱）
git push -u origin feat/<slug>
gh pr create --base main --head feat/<slug> --title "<type>(<scope>): <標題> [E?-??]" \
  --body "E?-??（<layer/subsystem>, <risk>, 消費 <deps>）。CHG-20260724-<slug>。<一句設計>；相位1平台中立；<N> gtest。I1.<n> 實作、I1 稽核✓。"
# 3) 回收 worktree
git worktree remove --force .claude/worktrees/agent-<id>; git worktree prune
```

- 稽核要點：所有檔在 write_scope + COMMON 內、有 `docs/structure/units/<slug>.md`（**不得動 `directory.md`**）、platform/相位1 單元無 `#ifdef`/win32/cocoa/真實後端、`src/**` 單元無 `backend/win32|cocoa/` 目錄。
- 派工一律用 **`Agent` + `isolation: worktree`**（見第 5 節）。派工簡報必附：先讀 BUILD.md、上游契約路徑、write_scope、相位1平台中立、CMake 樣板、要有可跑 gtest、CHG 表頭、**完成後盡早 commit**、不 push 不開 PR、墊片巨集單次求值。

## 5. 血淚陷阱（務必遵守，皆已踩過）

1. **並行派工必用 `isolation: worktree`**：否則多個 subagent 共用同一 HEAD/index，commit 落錯分支、`git add -A` 互相污染。若已污染，用隔離臨時 index plumbing 重建：`GIT_INDEX_FILE=tmp; git read-tree main; git update-index --add --cacheinfo <mode>,<blob>,<path>（只加該單元 blob）; write-tree; commit-tree -p main; git branch -f feat/<slug>`（分支不得為 checked-out）。詳見全域記憶 `autopilot-parallel-dispatch-worktree-isolation`。
2. **G2「CHG linked」閘門檢查 PR 內文**（`echo "<body>" | grep -qE 'CHG-[0-9]{8}-'`）：PR 內文**必須**含 `CHG-YYYYMMDD-` 字樣，否則 gate 6 秒內紅燈、BLOCKED 不合併。修法：`gh pr edit <n> --body "...CHG-20260724-<slug>..."` 後**推一個空 commit**觸發 synchronize 重跑（re-run 會用舊 body 無效）：
   ```bash
   tip=$(git rev-parse feat/<slug>); tree=$(git rev-parse feat/<slug>^{tree})
   new=$(printf 'chore: re-trigger gate' | git commit-tree "$tree" -p "$tip"); git update-ref refs/heads/feat/<slug> "$new"; git push origin feat/<slug>
   ```
3. **session 額度中止**：subagent 可能因 API session limit 中途 fail（訊息含 "hit your session limit · resets <time>"）。處置：盤點各分支 `git rev-list --count origin/main..feat/<slug>`——有 commit 者照常 push/PR；0 commit 者 `git worktree remove --force` + `git branch -D` 後重派。額度重置後使用者會說 "keep running"。
4. 本機**無 cmake/gtest**，subagent 以 `g++ -std=c++17 -Wall -Wextra` + 自寫 scratchpad gtest 墊片驗證（墊片不進 repo；正式 CTest 由 CI ubuntu-latest FetchContent GoogleTest 跑）。務必叮囑墊片 `ASSERT_*`/`EXPECT_*` **只對引數求值一次**（否則 `register_on` 被呼叫兩次假象）。
5. 提交護欄：含 `&&` 串接的 `git commit` 有時被誤判為 `--no-verify` 而擋下 → 拆成獨立 `git add` + `git commit -m`（多 `-m` 或 `-F 檔`），不要 heredoc。worktree shell 護欄擋含 `cd`/重導的複合命令 → 用全絕對路徑單一命令。
6. 每次 Write/Edit 會觸發 **Fact-Forcing Gate**（要陳述 4 事實）——正常流程，逐一提供即過。

## 6. 接下來的路線（可派 low 池變薄，需解鎖 wave）

- ready 池已縮到個位數 low + 2 medium，因多數剩餘單元相依尚未合併的 wave 3–8。
- **下一關鍵：medium 三件套**（解鎖大量下游）：
  - **E1-25** kernel 後端契約測試組（`tests/contract/`，medium, lb=3）——**需先改 root `CMakeLists.txt` 讓它 glob `tests/contract/`**（bootstrap infra 調整，允許；開一張 CHG）。
  - **E1-24** null 後端參考實作（medium, lb=0，任何平台可跑）。
  - **E1-22** 建置期能力閘控 lint（low, lb=1）——NFR-02/03 的 lint，可能需與 CMake/CI 整合（BUILD.md 已註「待 E1-22 lint 上線」）。
  - 這三個是相位1 kernel 後端契約的收口，建議接手後優先處理；medium 依標準指令 CI 綠即自動合併（G6 會掛 `halt:awaiting-human` label 但 gate 仍綠，I1 直接 `gh pr merge <n> --squash --delete-branch`）。
- 之後持續用第 1 節 ready 腳本滾動派工，一波約 6 個，直到 176 全合併。

## 7. 全部 task 完成後（延後項，勿提前做）

- **【使用者指定 2026-07-28】扁平化目錄結構**：去掉 per-unit 代號層。現況 `modules/<subsystem>/<slug>/<file>.cpp`（如 `modules/sysinfo/e2_03/cpu_load.cpp`）多了一層 `e2_03/`；目標 `modules/<subsystem>/<file>.cpp`（如 `modules/elements/xxxxx.cpp`），原始碼直接置於子系統目錄下。連帶需處理：各 per-unit CMakeLists 合併/重整（同子系統多單元共目錄）、root CMake 的 GLOB 深度（現 `modules/*/*/CMakeLists.txt` 深度2）、include 路徑、`docs/structure/units/` 結構註記。影響全庫，務必**所有單元合併完成後**再一次性重構（開專屬 CHG）。同規則適用 `engine/<subsystem>/<slug>/` 與 `src/kernel/<slug>/`。
- 4 個未命名 validator + E8-06 收進 backlog（新層 `example`）；E8-04 提升 P0；artifact→`examples/` 遷移；E7-01 數值溢位非阻斷缺陷修復。
- **最終**：依使用環境建置 CPU/GPU Usage Widget 驗證器並測試（組裝自 E2-03 CPU + E2-05 GPU + E4 繪製 + E6-01 命令 + E7 格式，不改 src/）。

## 8. 參考

- 全域記憶：`autopilot-i1-coordinator-role`、`autopilot-parallel-dispatch-worktree-isolation`（`~/.claude/projects/-Users-weiss-Documents-GitHub-desktop-shell/memory/`）。
- 建置慣例：`docs/backlog/BUILD.md`。單一派工來源：`docs/backlog/units.json`。8 道閘門：`.github/workflows/governance.yml`。
