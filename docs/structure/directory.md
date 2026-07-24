# 目錄結構

- Date: 2026-07-24 (UTC+0)
- 對應：`docs/architecture.md`（分層與擴充點）、`docs/backlog/units.json`（`write_scope` 為唯一真實來源）

> 本檔是 G4「動了 `src/` 就得同步 `docs/structure/`」的比對對象。
> 目錄若與 `units.json` 的 `write_scope` 不一致，以 `write_scope` 為準並回頭修本檔。

## 現況（由 `units.json` 的 `write_scope` 反推，機器可複算）

```
src/                  平台核心
├── kernel/           surface kernel、四參數 profile、後端（backend/null、backend/win32…）
├── sensors/          監測子系統（指標提供者）
├── render/           繪製子系統（paint / transform / clip / text 基座）
├── format/           描述子系統（宣告式格式、變數、求值）
├── events/           事件
├── command/          命令匯流排與分派
├── actuators/        致動器（音量、電源等副作用）
├── script/           腳本引擎、模組載入
├── package/          套件格式、manifest、持久化
├── ipc/              行程間介接
├── host/             宿主整合
└── common/           共用基礎

tests/
├── contract/         跨後端契約測試（不得含平台分支，由 backend_guard 強制）
├── e1/ … e12/        對應 src/ 各子系統
└── c1/ … c4/         對應組件層

content/              組件層：profiles（C1）、widgets（C2）、C3 內容
apps/                 組件層：獨立應用型組件（C4）

docs/                 治理文件（changes / acceptance / structure / knowledge / worklog / backlog）
scripts/              治理工具（plan / scope_check / backend_guard / halt_gate）
```

## 分層對應

| layer | 位置 | 項數 | 說明 |
|---|---|---|---|
| **core** | `src/**` | 152 | 平台核心，提供組件消費的能力 |
| **component** | `content/**`、`apps/**`（模組 C1–C4） | 24 | 驗證器：證明擴充點真的能被外部使用 |

## 目標結構（`docs/architecture.md` 指定，**尚未遷移**）

架構文件要求「`src/` 只放平台，模組放 `modules/`，範例放 `examples/`」，
理由是讓「第三方能不能加」變成目錄結構就看得出來的事。

| 項目 | 現況 | 目標 |
|---|---|---|
| 第一方模組 | `content/`、`apps/` | `modules/` |
| 範例 | （無） | `examples/`，含 `examples/hello-module/`（`E8-06` 的受測對象） |

**遷移尚未執行。** 需要改動 24 個組件單元的 `write_scope`，屬設計決策，
待決後開 CHG 執行。在此之前 `content/` 與 `apps/` 仍為組件層的正式位置。

## 其餘三個結構

`ai-sdlc-autopilot` 的四結構中，本檔為「目錄結構」。
邏輯結構、設計結構、資料結構**尚未產出** —— 待五個擴充點的契約
（`E2-01` / `E4` / `E6-01` / `E1` / `E8-01`+`E8-04`）定稿後補上，
在此之前產出的內容只會是猜測。
