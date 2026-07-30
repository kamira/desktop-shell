// E7-12 設定值寫回 — 把修改後的值寫回宣告式文件並序列化回文字格式（engine 層 / 平台中立）
//
// 本單元是 E7-01 宣告式格式核心的**應用**：不自造格式模型，完全消費 E7-01 的
// `Value` / `Document` / `FormatVersion` 契約。用途是「設定持久化」——
//   1. 以**路徑**（map 鍵 / list 索引）定位文件節點；
//   2. **更新既有值**或**新增鍵 / 附加元素**（純函式：回傳新的 Value 樹，不就地改寫，
//      與 E7-01 的不可變 Value 值語意一致）；
//   3. **序列化**整份文件回 E7-01 的文字宣告格式，供寫回設定檔。
//
// 核心不變式（round-trip）：`parse(text)` → `set_value(...)` → `serialize(...)` →
// 再 `parse(...)` 應得到型別化上一致的文件（見測試）。序列化輸出即為 E7-01 能再解析的合法輸入。
//
// 設計原則（承 E7-01）：
//   - **平台中立、純邏輯**：無任何 `#ifdef` / 系統呼叫 / 真實後端。
//   - **不靜默失敗**（NFR-04 精神）：路徑套用的契約違反（對非 map 用鍵定位、對非 list 用索引、
//     索引越界、於缺失的中繼段用索引自動建立容器）一律 throw `std::runtime_error`——明確失敗，
//     而非回傳可疑結果。
//
// 已知限制（明確記錄，非靜默）：
//   - **空容器不 round-trip 為同型**：E7-01 文法中 `key:`（其下無更深縮排）解析為 `null`，
//     故序列化一個空 Map / 空 List 會在再解析時塌成 Null。設定寫回實務極少產生空容器；
//     若需保留空容器語意，應以顯式哨兵值表達。
//   - **map 鍵不支援引號**：E7-01 的鍵是裸 token，故鍵不得含 `:` / 換行 / 前後空白等；
//     此為 E7-01 文法限制，寫回端假設鍵為單純設定鍵名。值（value）無此限制（含轉義字串）。
#ifndef DS_ENGINE_E7_12_WRITEBACK_HPP
#define DS_ENGINE_E7_12_WRITEBACK_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "document.hpp"  // E7-01：Value / Document / FormatVersion（相依 target e7_01）

namespace ds::format {

// -----------------------------------------------------------------------------
// 路徑：定位文件內某節點的一段步進序列
// -----------------------------------------------------------------------------

// 單一路徑段：map 鍵（Key）或 list 索引（Index）。
struct PathSegment {
    enum class Kind { Key, Index };

    Kind kind = Kind::Key;
    std::string key;         // 當 kind == Key 有效。
    std::size_t index = 0;   // 當 kind == Index 有效。

    static PathSegment of_key(std::string k);
    static PathSegment of_index(std::size_t i);

    bool operator==(const PathSegment& o) const noexcept;
    bool operator!=(const PathSegment& o) const noexcept { return !(*this == o); }
};

// 一條由多段組成的路徑（空路徑 = 指向根本身）。
using Path = std::vector<PathSegment>;

// 解析字串路徑為 Path。語法：鍵以 '.' 分隔，list 索引以 `[n]` 標示（可接在鍵後或彼此相接）。
//   例："window.width"、"layers[0].name"、"tags[1]"、"grid[2][3]"。
// 回傳 false = 語法錯誤（空鍵、未閉合的 '['、括號內非數字等；不得靜默）；out 內容未定義。
bool parse_path(const std::string& text, Path& out);

// -----------------------------------------------------------------------------
// 設定值寫回：以路徑定位、更新 / 新增值（純函式，回傳新樹）
// -----------------------------------------------------------------------------

// 在 root 的 path 位置設定 value，回傳一棵**新的** Value（root 不被就地改寫）。
//   - 空 path：直接回傳 value（取代整棵）。
//   - Key 段：node 須為 Map。鍵存在則更新，不存在則**新增**（附加於尾端，保序）。
//   - Index 段：node 須為 List。索引 < size 則更新；索引 == size 且為**最後一段**則附加於尾端。
//   - 中繼缺失（非最後一段且鍵不存在）：自動建立空 Map 續行——**但下一段須為 Key**；
//     若下一段為 Index（無法對缺失容器自動決定 list 長度）→ throw。
// 契約違反（型別不符 / 索引越界 / 中繼以索引建立）一律 throw std::runtime_error（可定位訊息）。
Value set_value(const Value& root, const Path& path, Value value);

// 便捷多載：以字串路徑呼叫（先 parse_path，語法錯誤 → throw std::runtime_error）。
Value set_value(const Value& root, const std::string& path, Value value);

// -----------------------------------------------------------------------------
// 序列化：文件 / 節點 → E7-01 文字格式
// -----------------------------------------------------------------------------

// 序列化單一 Value 節點為 E7-01 文字格式（不含 format_version 行）。
//   - 根 Value 通常為 Map；亦可序列化 List / 純量（供進階呼叫端 / 測試）。
//   - 純量的字串會依 E7-01 型別推斷規則決定是否加引號並轉義（確保再解析型別不漂移）。
//   - 縮排每層 2 空白，與 E7-01 文件範例一致。輸出以換行結尾（非空時）。
std::string serialize_value(const Value& root);

// 序列化整份文件回 E7-01 文字格式：首行 `format_version: major.minor`，其後為 root 內容。
// 產出即為 E7-01 `parse()` 能再解析的合法輸入（round-trip）。
std::string serialize(const Document& doc);

// 便捷多載：以 root（須為 Map）+ 版本組成文件並序列化。
std::string serialize(const Value& root, const FormatVersion& version);

}  // namespace ds::format

#endif  // DS_ENGINE_E7_12_WRITEBACK_HPP
