// E7-03 段落變數 — 宣告式文件內的「變數段落」定義與展開（平台中立 / engine 層）
//
// 本單元建構於 E7-01（`Value` / `Document` 資料模型）與 E7-02（`VariableScope` /
// `resolve()` 變數替換引擎）之上。兩者的分工如下：
//
//   - E7-02 提供替換「引擎」：你**在程式外**建好一個 `VariableScope`（逐一 `define`），
//     再呼叫 `resolve()` 把節點樹裡的 `${name}` 引用替換掉。變數來自何處，E7-02 不管。
//   - E7-03（本單元）補上「**變數段落**」這一層：讓變數**宣告於文件自身之內**——文件的
//     某個具名區段（預設鍵 `vars`）本身就是一張「名稱 → 值」的映射；文件的其餘內容以
//     `${name}` 引用它。本單元負責把該段落抽出、建成 `VariableScope`，再委派 E7-02 展開
//     其餘內容。於是「宣告一次、處處引用」可完全在單一宣告式文件裡表達，無需外部程式碼。
//
// 這正是「段落變數」的語意：
//     format_version: 1.0
//     vars:                     # ← 變數段落（本單元識別並抽出）
//       base: /opt/app
//       name: hello
//     bin:  ${base}/bin         # ← 其餘內容引用段落內的變數
//     title: "app: ${name}"     # ← 字串內插
//
// 展開後（預設 strip_vars=true，段落本身自輸出移除）：
//     bin:   /opt/app/bin
//     title: "app: hello"
//
// 設計原則（延續 E7-01 / E7-02）：
//   - **平台中立、純邏輯**：無任何 `#ifdef`、系統呼叫或真實後端。
//   - **消費上游、不重造輪子**：型別模型沿用 E7-01 `Value`；替換 / 巢狀引用 / 循環偵測 /
//     字串內插 / 型別保留全數委派 E7-02 `resolve()`；錯誤模型沿用 E7-02 `ResolveError` /
//     `ResolveResult` / `DocumentResolveResult`。本單元只新增「從段落建作用域」這薄薄一層。
//   - **不靜默失敗**（NFR-04 精神）：變數段落若不是映射 → 明確回報；引用到未定義變數、
//     循環引用、未終止 `${`、容器內嵌字串化等 → 沿用 E7-02 的明確 `ResolveError`（帶肇因
//     變數名，定位到出處），絕不靜默替成空字串或跳過。
//
// 語意細節：
//   - 段落內的變數值本身可再含 `${...}`（互相/巢狀引用）；由 E7-02 於使用時**惰性**遞迴
//     求值，故段落內變數的宣告**順序無關**。循環引用由 E7-02 偵測並明確回報。
//   - `strip_vars=true`（預設）：輸出為「其餘成員」（段落已移除），逐一展開。
//   - `strip_vars=false`：輸出保留段落成員，且段落內的值同樣被展開。
//   - `parent`：可鏈接一個外層 `VariableScope`（如全域/環境變數）；段落內同名變數遮蔽父層。
//   - root 非映射時：無段落可抽，直接以（僅含 parent 的）作用域展開整棵樹。
#ifndef DS_ENGINE_E7_03_SECTION_VARS_HPP
#define DS_ENGINE_E7_03_SECTION_VARS_HPP

#include <string>

#include "variables.hpp"  // E7-02：VariableScope / resolve / ResolveError / *Result
                          //（並透過它取得 E7-01：Value / Document）

namespace ds::format {

// 變數段落的預設鍵名。可經 `ExpandOptions::vars_key` 覆寫。
inline constexpr char kDefaultVarsKey[] = "vars";

// 段落變數展開選項。
struct ExpandOptions {
    // 變數段落所在的 map 鍵名（於 root 映射的頂層尋找）。預設 "vars"。
    std::string vars_key = kDefaultVarsKey;
    // 是否自輸出移除變數段落本身（預設 true：段落只為宣告用途，不出現在展開結果）。
    bool strip_vars = true;
    // 可選的外層作用域（不取得所有權，須於呼叫期間有效）。段落變數遮蔽父層同名變數。
    const VariableScope* parent = nullptr;
};

// 由一個變數段落（必須是 Map 型別的 Value，鍵為變數名、值為變數值）填入 `scope`。
//   - `scope` 由呼叫端先行建構（可帶所需父作用域）；本函式依插入序把每個成員 `define` 進去。
//   - 值以**原樣（未展開）**插入；成員間的巢狀 / 互相引用由 E7-02 於使用時惰性求值。
//   - `section` 非 Map → 回傳 false 並填 `err`（不靜默）；空 Map 為合法（回傳 true）。
bool build_scope(const Value& section, VariableScope& scope, ResolveError& err);

// 對單一 Value 節點樹做「段落變數」展開。
//   - `root` 為 Map：於頂層尋找 `opts.vars_key` 段落，建成作用域（鏈接 `opts.parent`），
//     再以 E7-02 `resolve()` 展開其餘（或全部，視 `strip_vars`）成員；回傳結果 Map。
//   - `root` 非 Map：無段落可抽，直接以僅含 `opts.parent` 的作用域 `resolve()` 整棵樹。
//   - 段落非 Map、或任一引用未定義 / 循環 / 未終止 / 容器內嵌字串化 → failure（帶訊息/變數名）。
ResolveResult expand(const Value& root, const ExpandOptions& opts = ExpandOptions{});

// 對整份 Document 做「段落變數」展開：展開 `doc.root`（語意同上），`format_version` 原樣保留。
DocumentResolveResult expand(const Document& doc, const ExpandOptions& opts = ExpandOptions{});

}  // namespace ds::format

#endif  // DS_ENGINE_E7_03_SECTION_VARS_HPP
