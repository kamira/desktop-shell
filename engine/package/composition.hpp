// E9-03 可互換元件組合 — 組合層 API（平台中立 / 純邏輯 / engine 層）
//
// 語意：以 E9-01 定義的「元件」（`ds::package::Package`，= manifest + 內容清單）為單位，
// 讓元件能**以可互換方式組合**：一個組合（Composition）由若干具名「插槽 / 角色」
// （ComponentSlot）構成；針對某個插槽，可插入**任一符合該插槽介面**的元件，並且：
//   1. 綁定前於組合層驗證相容性（compatibility），不相容一律**明確報錯、不靜默**；
//   2. 允許在已綁定的插槽上**替換**（swap）為另一相容元件；
//   3. 能在一組候選元件中**列舉**出與某插槽相容的候選（compatible_candidates）；
//   4. 插槽可處於**空狀態**（尚未綁定），組合完整性驗證會指出未綁定的插槽。
//
// 插槽介面（interface）的定義 — 直接建於 E9-01 的元件模型之上：
//   一個插槽宣告一組 `required_kinds`（所需資源類別）。一個元件（Package）與該插槽**相容**
//   iff：(a) 該元件本身結構完整（複用 E9-01 `validate_package`）；且 (b) 對插槽宣告的每個
//   required_kind，元件的內容清單（entries）至少提供一項該 kind 的資源。
//   required_kinds 為空的插槽 = 無額外介面要求，接受任一結構完整的元件。
//
// 設計原則（與 E9-01 / E9-02 一致）：
//   - **平台中立、純邏輯**：不含任何 `#ifdef`、系統呼叫或真實後端。
//   - **失敗不靜默**：相容性檢查與綁定/替換操作失敗時，一律回傳帶人類可讀原因的結果物件。
#ifndef DS_ENGINE_E9_03_COMPOSITION_HPP
#define DS_ENGINE_E9_03_COMPOSITION_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "package.hpp"  // E9-01（由 target_link_libraries PUBLIC e9_01 傳遞 include 路徑）

namespace ds::package {

// 一個插槽 / 角色：組合中的一個可互換位置，帶其對元件的介面要求。純資料。
struct ComponentSlot {
    std::string id;                          // 插槽 id / 角色名（組合內唯一，非空）。
    std::vector<std::string> required_kinds; // 元件內容清單須提供的資源類別；空 = 無額外要求。
};

// 相容性檢查結果：相容，或帶不靜默的原因。二者互斥。
struct CompatibilityResult {
    bool compatible = false;
    std::string reason;  // 不相容時的人類可讀原因；相容時為空。

    explicit operator bool() const noexcept { return compatible; }

    static CompatibilityResult ok() { return {true, {}}; }
    static CompatibilityResult no(std::string why) { return {false, std::move(why)}; }
};

// 綁定 / 替換操作結果：成功，或帶不靜默的原因（如插槽不存在、狀態不符、元件不相容）。
struct BindResult {
    bool ok = false;
    std::string message;  // 失敗時的人類可讀原因；成功時為空。

    explicit operator bool() const noexcept { return ok; }

    static BindResult success() { return {true, {}}; }
    static BindResult failure(std::string why) { return {false, std::move(why)}; }
};

// 檢查一個元件（Package）是否符合某插槽的介面。見檔首「插槽介面」說明。
// 不相容一律帶原因回傳（結構無效 / 缺少所需 kind），不靜默。
CompatibilityResult check_compatibility(const ComponentSlot& slot, const Package& component);

// 一個可互換元件組合：一組具名插槽，各插槽可綁定 / 替換 / 清空一個相容元件。
class Composition {
public:
    // 註冊一個插槽。id 須非空且在組合內唯一。
    //   - id 為空 → failure；id 與既有插槽重複 → failure。成功後該插槽初始為「空」。
    BindResult add_slot(const ComponentSlot& slot);

    // 將元件綁定到「目前為空」的插槽（首次綁定）。
    //   - 插槽不存在 / 插槽已綁定（請改用 swap）/ 元件不相容 → 皆 failure(帶原因)，狀態不變。
    BindResult bind(const std::string& slot_id, const Package& component);

    // 於「目前已綁定」的插槽上替換為另一元件（可互換）。
    //   - 插槽不存在 / 插槽尚為空（請改用 bind）/ 元件不相容 → 皆 failure(帶原因)，狀態不變。
    BindResult swap(const std::string& slot_id, const Package& component);

    // 將已綁定的插槽清回空狀態。插槽不存在 → failure；插槽本已空 → failure(不靜默)。
    BindResult unbind(const std::string& slot_id);

    // 查詢：組合是否含此插槽。
    bool has_slot(const std::string& slot_id) const noexcept;

    // 查詢：此插槽目前是否已綁定元件（false = 空狀態或插槽不存在）。
    bool is_bound(const std::string& slot_id) const noexcept;

    // 取得插槽目前綁定的元件；插槽不存在或為空 → nullptr。
    const Package* bound_component(const std::string& slot_id) const noexcept;

    // 取得插槽定義；插槽不存在 → nullptr。
    const ComponentSlot* slot(const std::string& slot_id) const noexcept;

    // 依註冊順序列出所有插槽 id。
    std::vector<std::string> slot_ids() const;

    // 在一組候選元件中，列舉與某插槽相容者，回傳其在 candidates 中的索引（依原順序）。
    //   插槽不存在 → 空清單。不修改組合狀態。
    std::vector<std::size_t> compatible_candidates(
        const std::string& slot_id, const std::vector<Package>& candidates) const;

    // 組合完整性驗證：每個插槽皆已綁定，且每個綁定仍相容。
    //   有未綁定插槽 → failure(指名該插槽)；某綁定已不相容 → failure(帶原因)。空組合視為完整。
    BindResult validate() const;

private:
    struct Binding {
        ComponentSlot slot;      // 插槽定義。
        bool bound = false;      // 是否已綁定。
        Package component;       // bound 為 true 時有效。
    };

    // 線性查找（插槽數通常很小；保留註冊順序以利穩定列舉）。回傳索引或 npos。
    std::size_t index_of(const std::string& slot_id) const noexcept;

    std::vector<Binding> bindings_;
};

}  // namespace ds::package

#endif  // DS_ENGINE_E9_03_COMPOSITION_HPP
