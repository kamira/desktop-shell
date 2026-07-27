// E7-02 變數系統 — 宣告式格式的變數定義與引用替換（平台中立 / engine 層）
//
// 本單元建構於 E7-01 的資料模型（`Value` / `Document`）之上：提供一個變數
// **作用域（scope）/ 表**，並把文件（或任一 `Value` 節點樹）裡的變數引用
// 解析、替換為對應的變數值。屬「描述子系統」的一環——設定 / profile / 內容
// 常需在多處複用同一組值（路徑、名稱、尺寸），變數系統讓宣告一次、處處引用。
//
// 設計原則（延續 E7-01）：
//   - **平台中立、純邏輯**：不含任何 `#ifdef`、系統呼叫或真實後端。
//   - **未定義變數不得靜默**（NFR-04 精神）：引用到未定義的變數一律回報帶
//     訊息（含變數名）的 `ResolveError`；絕不靜默替成空字串或跳過。
//   - **消費上游、不重造輪子**：型別模型、容器、深層相等全部沿用 E7-01 的 `Value`。
//
// 引用語法（於**字串純量**內）：
//     ${name}            引用名為 name 的變數值。
//   - 若整個字串**恰好**是單一引用 `${name}`（前後無其他字元），替換結果**保留
//     變數值的原生型別**（數字仍是數字、清單仍是清單）——即「引用變數值」。
//   - 若引用**內嵌**於文字中（前後有字元，或同一字串含多個引用），每個引用替換為
//     其變數值的**字串呈現**，最終結果為字串純量。內嵌引用到容器型別（List / Map）
//     無法字串化 → 明確回報錯誤（非靜默）。
//   - 轉義：`$$` 收斂為字面 `$`；故 `$${` 產生字面 `${`（不觸發引用）。
//     單獨的 `$`（其後非 `$` 亦非 `{`）為字面 `$`。
//   - `${` 未以 `}` 收尾 → 未終止引用錯誤；`${}`（空名）→ 空變數名錯誤。
//
// 巢狀語意：
//   - **巢狀作用域**：`VariableScope` 可帶父作用域；查找在本層找不到時沿父鏈上溯
//     （子層可遮蔽父層同名變數）。
//   - **巢狀引用**：變數的值本身可再含 `${...}` 引用；解析時對其遞迴求值（於同一
//     作用域）。以「求值中」堆疊偵測**循環引用**並明確回報（非無限遞迴 / 非靜默）。
//   - 遞迴進入 List / Map 的每個元素 / 成員值（鍵不替換），整棵樹一致處理。
#ifndef DS_ENGINE_E7_02_VARIABLES_HPP
#define DS_ENGINE_E7_02_VARIABLES_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "document.hpp"  // E7-01：ds::format::Value / Document

namespace ds::format {

// -----------------------------------------------------------------------------
// 變數作用域 / 表
// -----------------------------------------------------------------------------

// 一個變數作用域：名稱 → Value 的有序表，鍵唯一，可選帶父作用域。
//
// 查找語意：`find` / `has` 先查本層，找不到沿父鏈上溯；子層同名變數遮蔽父層。
// 本層插入保序（`names()` 依插入序回傳），與 E7-01 Map 的可重現輸出風格一致。
class VariableScope {
public:
    using Entry = std::pair<std::string, Value>;

    VariableScope() = default;
    // 建立以 parent 為父的巢狀作用域。parent 必須在本物件存活期間有效（不取得所有權）。
    explicit VariableScope(const VariableScope* parent) : parent_(parent) {}

    // 定義 / 覆寫本層變數（同名則就地覆寫其值，保留原插入位置）。
    void define(std::string name, Value value);

    // 查找（含父鏈）：不存在回 nullptr。
    const Value* find(const std::string& name) const;
    // 是否可解析（含父鏈）。
    bool has(const std::string& name) const { return find(name) != nullptr; }

    // 僅查本層（不上溯父鏈）。
    bool has_local(const std::string& name) const;
    // 本層變數名（依插入序）。
    std::vector<std::string> names() const;
    // 本層變數數（不含父層）。
    std::size_t size() const noexcept { return vars_.size(); }

    const VariableScope* parent() const noexcept { return parent_; }

private:
    const VariableScope* parent_ = nullptr;
    std::vector<Entry> vars_;  // 有序、鍵唯一。
};

// -----------------------------------------------------------------------------
// 解析結果 / 錯誤（不靜默失敗）
// -----------------------------------------------------------------------------

// 變數替換錯誤——引用未定義、循環引用、未終止 `${`、空變數名、
// 容器型別內嵌字串化等，一律帶人類可讀訊息；`variable` 為肇因變數名（若適用）。
struct ResolveError {
    std::string message;   // 人類可讀原因。
    std::string variable;  // 肇因變數名（無關時為空字串）。
};

// 對單一 Value 做變數替換的結果：成功持 Value，失敗持 ResolveError。二者互斥。
class ResolveResult {
public:
    static ResolveResult success(Value v);
    static ResolveResult failure(ResolveError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    const Value& value() const { return value_; }        // 僅 ok() 為 true 時有效。
    const ResolveError& error() const { return error_; }  // 僅 ok() 為 false 時有效。

private:
    ResolveResult() = default;
    bool ok_ = false;
    Value value_;
    ResolveError error_;
};

// 對整份 Document 做變數替換的結果：成功持 Document（版本欄位原樣保留，root
// 內容已替換），失敗持 ResolveError。
class DocumentResolveResult {
public:
    static DocumentResolveResult success(Document d);
    static DocumentResolveResult failure(ResolveError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    const Document& document() const { return document_; }
    const ResolveError& error() const { return error_; }

private:
    DocumentResolveResult() = default;
    bool ok_ = false;
    Document document_;
    ResolveError error_;
};

// -----------------------------------------------------------------------------
// 替換入口
// -----------------------------------------------------------------------------

// 解析並替換一個 Value 節點樹中的所有變數引用（遞迴進入 List / Map）。
// 見檔首語意。任一未定義 / 循環 / 未終止引用 / 容器內嵌字串化 → failure（帶變數名）。
ResolveResult resolve(const Value& value, const VariableScope& scope);

// 解析並替換整份 Document 的 root 內容；`format_version` 原樣保留。
DocumentResolveResult resolve(const Document& doc, const VariableScope& scope);

}  // namespace ds::format

#endif  // DS_ENGINE_E7_02_VARIABLES_HPP
