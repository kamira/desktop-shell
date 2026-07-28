// E9-05 佈局存檔與還原 — 桌面元件佈局的具名存檔 / 還原（engine 層 / 平台中立）
//
// 語意：擷取「目前桌面佈局」——哪些元件、各元件的位置 / 大小 / 設定——序列化為
// E7-01 宣告式格式（透過 E7-12 `serialize`），存於可注入的儲存後端；日後可依名稱
// 還原，重建出型別化一致的佈局狀態。支援多個具名佈局（profile）：存 / 取 / 列舉 / 刪除。
//
// 設計原則（承 E7-01 / E7-12）：
//   - **平台中立、純邏輯**：無任何 `#ifdef` / 系統呼叫 / 真實檔案 I/O。佈局狀態以抽象
//     資料（`LayoutState`）表示；持久化走可注入的 `LayoutStorage` 抽象後端，預設實作為
//     純記憶體（`MemoryLayoutStorage`）。相位 1 平台中立：真實磁碟後端留待後續相位。
//   - **不重造格式**：序列化 / 反序列化完全消費 E7-01 的 `Value` / `Document` 契約，
//     並以 E7-12 的 `serialize` 產生宣告式文字。存檔文字即 E7-01 `parse()` 能再解析的合法輸入。
//   - **不靜默失敗**（NFR-04 精神）：查無指定名稱的佈局（load / remove）一律 throw
//     `std::runtime_error`（明確可定位訊息），絕不回傳空 / 預設佈局掩蓋錯誤；反序列化時
//     結構違反（elements 非清單、缺 id / type、型別不符）同樣明確拋錯。
//
// 對外介面（一句話）：`LayoutStore` 以注入的 `LayoutStorage` 為後端，提供
//   `save(name, LayoutState)` / `load(name) -> LayoutState` / `list()` / `remove(name)`。
//
// NFR-02 遵循：本核心 API **不**內建絕對座標欄位或數字 z-order。單一元件的位置 / 大小 /
// 設定一律以宣告式 `properties`（`ds::format::Value` 之 Map）承載，由呼叫端自行決定其
// 座標語意（可為相對錨點等），存檔器對其內容不透明——故 API 面不出現絕對座標與 z-order。
#ifndef DS_ENGINE_E9_05_LAYOUT_STORE_HPP
#define DS_ENGINE_E9_05_LAYOUT_STORE_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "document.hpp"   // E7-01：Value / Document / FormatVersion（經 e7_12 PUBLIC 傳遞）
#include "writeback.hpp"  // E7-12：serialize（相依 target e7_12）

namespace ds::layout {

// -----------------------------------------------------------------------------
// 佈局狀態：抽象、平台中立的資料表示
// -----------------------------------------------------------------------------

// 佈局中的單一元件。純資料。
//   - id：元件在佈局內的穩定識別（例如 "clock.main"）；不得為空。
//   - type：元件類別 / 種類（例如 "clock" / "cpu_gauge"）；語意對本存檔器不透明；不得為空。
//   - properties：宣告式屬性（位置 / 大小 / 各項設定）——`ds::format::Value` 的 **Map**。
//     存檔器對其內容不透明，僅原樣序列化 / 還原（NFR-02：座標語意不由本 API 規定）。
struct LayoutElement {
    std::string id;
    std::string type;
    ds::format::Value properties;  // 恆視為 Map；預設 Null 會被當成空 Map 處理。

    bool operator==(const LayoutElement& o) const;
    bool operator!=(const LayoutElement& o) const { return !(*this == o); }
};

// 一次完整的桌面佈局：一組元件（保序）。純資料。
struct LayoutState {
    std::vector<LayoutElement> elements;

    bool operator==(const LayoutState& o) const { return elements == o.elements; }
    bool operator!=(const LayoutState& o) const { return !(*this == o); }
};

// -----------------------------------------------------------------------------
// 序列化 / 反序列化：LayoutState <-> 宣告式文字（透過 E7-01 / E7-12）
// -----------------------------------------------------------------------------

// 本存檔格式的版本欄位（寫入文件首行 `format_version`）。
inline constexpr ds::format::FormatVersion kLayoutFormat{1, 0};

// 將 LayoutState 序列化為 E7-01 宣告式文字（首行含 format_version）。使用 E7-12 `serialize`。
std::string serialize_layout(const LayoutState& state);

// 從宣告式文字反序列化回 LayoutState。解析 / 結構違反 → throw std::runtime_error（可定位）。
LayoutState deserialize_layout(const std::string& text);

// -----------------------------------------------------------------------------
// 儲存後端：可注入的抽象（相位 1 = 純記憶體，無真實檔案 I/O）
// -----------------------------------------------------------------------------

// 具名佈局文字的鍵值儲存抽象。實作決定持久化媒介（記憶體 / 未來相位的磁碟等）。
// 契約：以名稱為鍵，值為已序列化的佈局文字；put 覆寫既有；get 回傳是否存在。
class LayoutStorage {
public:
    virtual ~LayoutStorage() = default;

    // 寫入 / 覆寫 name 對應的佈局文字。
    virtual void put(const std::string& name, const std::string& text) = 0;

    // 讀取 name 對應的佈局文字；存在則寫入 out 並回 true，否則回 false（不拋錯——由上層決定語意）。
    virtual bool get(const std::string& name, std::string& out) const = 0;

    // name 是否存在。
    virtual bool has(const std::string& name) const = 0;

    // 移除 name；存在並移除回 true，不存在回 false（不拋錯——由上層決定語意）。
    virtual bool erase(const std::string& name) = 0;

    // 列舉所有名稱（實作可自訂順序；MemoryLayoutStorage 以名稱字典序回傳，穩定可測）。
    virtual std::vector<std::string> names() const = 0;
};

// 純記憶體後端：相位 1 預設實作，無真實檔案 I/O。名稱以字典序列舉（穩定）。
class MemoryLayoutStorage : public LayoutStorage {
public:
    void put(const std::string& name, const std::string& text) override;
    bool get(const std::string& name, std::string& out) const override;
    bool has(const std::string& name) const override;
    bool erase(const std::string& name) override;
    std::vector<std::string> names() const override;

private:
    std::map<std::string, std::string> data_;  // 有序 map：列舉自然為字典序。
};

// -----------------------------------------------------------------------------
// 佈局存檔器：多具名 profile 的存 / 取 / 列舉 / 刪除
// -----------------------------------------------------------------------------

// 以注入的 LayoutStorage 為後端，管理多個具名佈局（profile）。
// 不擁有後端生命週期（呼叫端負責 storage 的存活期涵蓋本物件使用期間）。
class LayoutStore {
public:
    // 注入儲存後端（不可為 null；為 null → throw std::runtime_error）。
    explicit LayoutStore(LayoutStorage& storage);

    // 存檔：擷取 state 序列化（E7-12）後以 name 存入後端。既有同名 → **覆寫**。
    //   - name 為空 → throw std::runtime_error（名稱須非空）。
    void save(const std::string& name, const LayoutState& state);

    // 還原：讀取 name 的佈局文字並反序列化回 LayoutState。
    //   - 查無 name → throw std::runtime_error（不靜默）。
    //   - 文字損毀 / 結構違反 → throw std::runtime_error（由 deserialize_layout 傳遞）。
    LayoutState load(const std::string& name) const;

    // 是否存在名為 name 的佈局。
    bool contains(const std::string& name) const;

    // 列舉所有已存佈局名稱（後端順序；MemoryLayoutStorage 為字典序）。
    std::vector<std::string> list() const;

    // 刪除名為 name 的佈局。查無 name → throw std::runtime_error（不靜默）。
    void remove(const std::string& name);

private:
    LayoutStorage& storage_;
};

}  // namespace ds::layout

#endif  // DS_ENGINE_E9_05_LAYOUT_STORE_HPP
