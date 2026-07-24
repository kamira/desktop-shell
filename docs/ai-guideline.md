# AI Guideline — 桌面 Shell 引擎

- Project: desktop-shell
- Branch: main
- Version: v1.0
- Date: 2026-07-23 (UTC+0)
- Status: Confirmed
- Skill: ai-sdlc-autopilot v1.16+

## 1. 背景與目標

建立一個常駐桌面的 shell 引擎，同時支援四種桌面呈現形態：系統監控 skin、
桌面角色（立繪 + 對話）、召喚式啟動面板、短暫浮層。四者共用同一個 surface kernel。

成功定義：同一行程內多種 surface profile 共存且互不干擾，並滿足常駐資源門檻。

## 2. 範圍

### 納入
`docs/backlog/units.json` 中 `priority` 為 `P0` / `P1` 的 123 個工作單元。

### 明確排除
- 桌面環境職責：工作區管理、系統匣接管、通知守護、鎖定畫面
- 開放式第三方生態：沙箱、崩潰隔離、線上分發與差分更新、創意工坊
- 相容既有格式：Rainmeter INI / skin、SHIORI 協定、SSTP 既有實作
- 完整排除清單見 `desktop-shell-scope-v1.md` 附錄 A（31 項）

## 3. 決策紀錄

| # | 決策 | 選擇 | 附加約束 |
|---|---|---|---|
| Q1 | 目標平台 | 先 Windows，後跨平台 | kernel 不得出現絕對座標與數字 z-order |
| Q2 | HTML 渲染引擎 | 獨立行程隔離、選配 | web widget 初版不接資料層 |
| Q3 | 內容生態 | 先自用，格式預留開放 | 格式帶版本欄位；manifest 具 requires/permissions |

## 4. 功能需求

見 `docs/backlog/units.json`（176 單元，機器可讀，含相依圖、承重度、風險分級、鎖定範圍）。
人類可讀版本：`desktop-shell-build-list-v4.md`。

## 5. 非功能需求（可驗收）

| ID | 需求 | 驗收方式 |
|---|---|---|
| NFR-01 | idle 記憶體與 CPU 門檻 | 常駐 30 分鐘後量測，寫入 ACC |
| NFR-02 | kernel 不得出現絕對座標 API | `E1-22` 建置期 lint，違反即建置失敗 |
| NFR-03 | 能力閘控 API 呼叫必須有 `has()` 保護 | 同上 |
| NFR-04 | 描述格式錯誤須定位到行，不得靜默失敗 | `E7-06` 單元測試 |
| NFR-05 | 熱重載：改設定檔即生效，不重啟 | `E7-07` 整合測試 |
| NFR-06 | 每個 PR 的變更必須落在該單元 `write_scope` 內 | CI `scope_check` |

## 6. 風險分級規則（機械化，不憑感覺）

`medium` 的判定條件（滿足其一）：
- 承重度（被依賴次數）≥ 8 —— 介面設計錯誤的擴散成本高
- 屬能力閘控項 —— CI 在單一平台上驗不出降級路徑
- 屬平台耦合的 kernel 原語（圖層 / alpha surface / 輸入策略）

其餘為 `low`。本專案無 `high`（無正式環境、無資料遷移、無金流、無憑證）。

現況：medium 16 項、low 160 項。

## 7. 驗收標準

- **low**：單元測試綠燈 + CHG 內留一行可重跑證據（inline 自驗）
- **medium**：單元測試綠燈 + V1 獨立驗收產出 ACC + 能力閘控項附降級路徑測試
- **階段一整體**：wave 0 的 18 個單元全數 Accepted，且 NFR-01 達標

## 8. 假設與待決

| # | 項目 | 狀態 |
|---|---|---|
| A1 | 引擎語言為 Qt/QML | 待 wave 0 驗證後確認；若 kernel 驗收不過需重議 |
| A2 | `desktop-shell-scope-v1.md` §5 的 6 個 `*` 判定 | 待覆核 |
| A3 | 是否砍 `啟動` 功能群（13 單元） | 未決；砍掉可移除 kernel 最易翻車的獨占焦點路徑 |
