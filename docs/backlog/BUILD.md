# 建置慣例 — C++ / CMake / CTest（每個 subagent 必讀）

- Date: 2026-07-24 (UTC+0)
- 語言決策見 `docs/ai-guideline.md` §3 Q4：不限語言、C/C++ 允許。本專案定案 **C++17**。

## 你（subagent）要在自己的 write_scope 內放什麼

以 `E1-21`（`src/kernel/e1_21/`，測試 `tests/e1/test_e1_21*`）為例：

```
src/kernel/e1_21/
├── CMakeLists.txt        # 你寫；根 CMakeLists 會自動 glob 收入
├── <你的實作>.hpp / .cpp
tests/e1/
└── test_e1_21.cpp        # 你的 gtest 測試（檔名必須 test_<slug>*）
```

**你只能寫這兩個 write_scope 路徑。** 根 `CMakeLists.txt` 是 bootstrap 基礎設施，
**不在你的範圍內，不得修改**（改了 G1 scope_check 直接紅燈）。

## 你的單元 CMakeLists.txt 樣板

```cmake
# src/kernel/e1_21/CMakeLists.txt
add_library(e1_21 STATIC capability_matrix.cpp)
target_include_directories(e1_21 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 測試檔在 tests/<module>/，以相對路徑指向（那也是你的 write_scope）
add_executable(test_e1_21 ${CMAKE_SOURCE_DIR}/tests/e1/test_e1_21.cpp)
target_link_libraries(test_e1_21 PRIVATE e1_21 GTest::gtest_main)
add_test(NAME e1_21 COMMAND test_e1_21)
```

- 純資料宣告型單元（如能力矩陣是 JSON/YAML）：實作放你的目錄，測試寫一個 gtest
  載入該檔並驗結構，一樣 `add_test` 註冊。**沒有註冊測試 = G3 紅燈**（真空綠燈防線）。
- 相依上游單元：`target_link_libraries(... PRIVATE <上游 target>)`；上游已合併，可讀不可改。

## 硬性約束（會被機器擋）

| 約束 | 由誰擋 |
|---|---|
| 只寫 write_scope 內的路徑 | G1 `scope_check` |
| 當前相位不得出現 `src/kernel/backend/win32|cocoa/` | G1b `backend_guard` |
| 較早階段不得依賴較晚階段 | G1c `stage_check` |
| 至少一個可跑的 CTest 測試 | G3（`ctest`，0 測試即紅） |
| 動了 `src|engine|modules/` 就得同步 `docs/structure/` | G4 |
| **核心 API 不得出現絕對座標與數字 z-order**（NFR-02） | 待 `E1-22` lint 上線；在那之前靠 code review |
| **能力閘控呼叫必須有 `has()` 保護**（NFR-03） | 同上 |

### G4 同步：寫**每單元獨立**的結構檔，**不要動共用的 `directory.md`**

滿足 G4 的方式：在 **`docs/structure/units/<slug>.md`** 新增一個**只屬於你這個單元**的結構註記
（`<slug>` = 單元 id 小寫底線，如 `e1_21`）。內容三五行：本單元的目錄位置、放了什麼、對外介面一句話。

**絕對不要編輯 `docs/structure/directory.md`。** 那是共用檔——多個並行單元同時改它會造成
合併衝突、甚至互相覆蓋彼此的註記。每單元一檔則永不衝突，且一樣滿足 G4（`docs/structure/` 有變更）。
`docs/structure/units/**` 已在 `scope_check` 的 COMMON 共用路徑內，可寫。

## 平台後端（相位 1 = Mac / null 期）

- `platform` 層對系統的操作：**只寫介面 + null 後端**，禁止 `win32/` `cocoa/` 真實後端
  （`backend_guard` 會擋）。契約測試不得含 `sys.platform` / `#ifdef _WIN32` 等平台分支。
- `engine` / `module` / `artifact` 層與平台無關，照常實作。
