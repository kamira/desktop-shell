# AI Guideline — 桌面 Shell 引擎

- Project: desktop-shell
- Branch: main
- Version: v1.1
- Date: 2026-07-24 (UTC+0)
- Status: Confirmed
- Skill: ai-sdlc-autopilot v1.1（基於 ai-sdlc v1.17）

## 0. 敘述性需求

本檔為**可驗收條款**。設計理由、取捨脈絡、失敗風險的敘述性說明見 `docs/requirements.md`；
分層、擴充點、階段與驗證器見 `docs/architecture.md`。

## 1. 背景與目標

建立一個常駐桌面的 shell 引擎，同時支援四種桌面呈現形態：系統監控 skin、
桌面角色（立繪 + 對話）、召喚式啟動面板、短暫浮層。四者共用同一個 surface kernel。

**本 repo 的產品是平台，不是桌面工具。** 驗收條件不是「能不能顯示一個 CPU 掛件」，
而是「能不能在不修改平台任何一行的前提下，讓外部加上一個 CPU 掛件」。

成功定義：同一行程內多種 surface profile 共存且互不干擾，滿足常駐資源門檻，
且各階段的範例組件在不修改 `src/` 的前提下載入運作。

## 2. 範圍

### 納入

`docs/backlog/units.json` 的工作單元。四層分層 `platform` / `engine` / `module` / `artifact`
（前二者合為平台本身，見 `docs/architecture.md`）。分四階段推進，每階段的完成定義包含
「該階段範例在不修改 `src/`、`engine/` 的前提下載入運作」。

| layer | 現況 | 內容 |
|---|---|---|
| **platform** | **30** | 對系統的操作（`src/**`）—— 換平台只有這裡要改 |
| **engine** | **64** | 平台中立的引擎邏輯（`engine/**` + 後端契約 E1-25/E1-26）—— 換平台一行不動 |
| **module** | **58** | 擴充點上的提供者（`subsystem`：`sysinfo` 25 / `elements` 22 / `actuators` 11） |
| **artifact** | **24** | 桌面上顯示的產出物，角色是驗證器 |
| 合計 | **176** | |

> `platform` + `engine` = 平台本身（先前合稱 `core`，94 項）。

> 草案原記「核心 160 / 組件 21 = 181」，與現況相差 5 個單元（僅 `E8-06` 有定義，其餘 4 個無出處）。
> **派工一律以 `units.json` 現況為準**，不得自行補足差額。

### 明確排除

- 桌面環境職責：工作區管理、系統匣接管、通知守護、鎖定畫面
- 開放式第三方生態：沙箱、崩潰隔離、線上分發與差分更新、創意工坊
- 相容既有格式：Rainmeter INI / skin、SHIORI 協定、SSTP 既有實作
- 完整排除清單見 `desktop-shell-scope-v1.md` 附錄 A（31 項）

## 3. 決策紀錄

| # | 決策 | 選擇 | 附加約束 |
|---|---|---|---|
| Q1 | 目標平台 | 先 Windows，後跨平台 | kernel 不得出現絕對座標與數字 z-order |
| Q2 | HTML 渲染引擎 | 獨立行程隔離、選配 | web widget 初版不接指標介面 |
| Q3 | 內容生態 | 先自用，格式預留開放 | 格式帶版本欄位；manifest 具 requires/permissions |
| Q4 | 實作語言 | **不限定任何語言**；擇能在各目標平台獲取系統資訊並操作系統者（C/C++ 允許） | 跨平台性由 API 面約束保證（NFR-02/03、`backend_guard`、契約測試），**不由語言保證** |

## 4. 功能需求

見 `docs/backlog/units.json`（**176 單元**，機器可讀，含相依圖、承重度、風險分級、
鎖定範圍、分層、階段）。人類可讀版本：`desktop-shell-build-list-v4.md`。

平台的實質內容是五個擴充點（詳見 `docs/architecture.md`）：
指標 `E2-01`、元件 `E4`、動作 `E6-01`、Profile `E1`、腳本 `E8-01`+`E8-04`。

## 5. 非功能需求（可驗收）

| ID | 需求 | 驗收方式 | 狀態 |
|---|---|---|---|
| NFR-01 | idle 記憶體與 CPU 門檻 | 常駐 30 分鐘後量測，寫入 ACC | 待實作 |
| NFR-02 | kernel 不得出現絕對座標 API | `E1-22` 建置期 lint，違反即建置失敗 | **CI 尚無建置步驟** |
| NFR-03 | 能力閘控 API 呼叫必須有 `has()` 保護 | 同上 | **同上** |
| NFR-04 | 描述格式錯誤須定位到行，不得靜默失敗 | `E7-06` 單元測試 | 待實作 |
| NFR-05 | 熱重載：改設定檔即生效，不重啟 | `E7-07` 整合測試 | 待實作 |
| NFR-06 | 每個 PR 的變更必須落在該單元 `write_scope` 內 | CI `scope_check.py` | **已實作 ✓** |
| NFR-07 | 組件載入不得需要修改核心 | `E8-06` 斷言 `src/` diff 為空 | 待 `E8-06` 入 units.json |
| NFR-08 | 較早階段不得依賴較晚階段 | CI `stage_check.py`（G1c） | **已實作 ✓** |

> NFR-02 / NFR-03 目前不可驗證：CI 只跑 `pytest`，沒有任何 C++/Qt 建置步驟。
> 這兩條在建置閘門補上之前是空的。

## 6. 風險分級規則（機械化，不憑感覺）

`medium` 的判定條件（滿足其一）：
- 承重度（被依賴次數）≥ 8 —— 介面設計錯誤的擴散成本高
- 屬能力閘控項 —— CI 在單一平台上驗不出降級路徑
- 屬平台耦合的 kernel 原語（圖層 / alpha surface / 輸入策略）

其餘為 `low`。本專案無 `high`（無正式環境、無資料遷移、無金流、無憑證）。

現況：**medium 16 項、low 160 項**（目標草案為 medium 21，差額隨那 5 個未定單元一併待補）。

## 7. 驗收標準

- **low**：單元測試綠燈 + CHG 內留一行可重跑證據（inline 自驗）
- **medium**：單元測試綠燈 + V1 獨立驗收產出 ACC + 能力閘控項附降級路徑測試
- **階段 A 整體**：wave 0 的 **18 個單元**全數 Accepted、NFR-01 達標，
  且 CPU/GPU Usage Widget 在不修改 `src/` 的前提下載入並顯示正確數值

## 8. 假設與待決

| # | 項目 | 狀態 |
|---|---|---|
| A1 | 實作語言 | **已定案（見 §3 Q4）**：不限語言，須能跨平台獲取與操作系統，C/C++ 允許。原 Qt/QML 假設解除 |
| A2 | `desktop-shell-scope-v1.md` §5 的 6 個 `*` 判定 | 待覆核 |
| A3 | 是否砍 `啟動` 功能群（13 單元） | 未決；砍掉可移除 kernel 最易翻車的獨占焦點路徑 |
| A4 | 目標 181 單元中未點名的 4 個 | **待補清單或產生器** |
| A5 | `artifact` 層（`content/`+`apps/`）是否遷至 `examples/` | 未執行；需改 24 個單元的 `write_scope`。`module` 層已於 CHG-08 遷至 `modules/<subsystem>/` |
| A6 | `E8-06` 的相依 / wave / 風險分級、`E8-04` 升 P0 | 待決後開 CHG 寫入 units.json |
