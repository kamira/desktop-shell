// E9-04 主題切換 — 視覺主題的註冊、執行期切換與變更通知（平台中立 / engine 層）
//
// 語意：本單元把「視覺主題」（顏色 / 樣式 / 資源集，以**宣告式資料**表示）封裝為一個
// 可在執行期切換的管理器。核心承諾：
//   1. **註冊多個主題**：每個主題以具名的 `ThemeData` 表示，其視覺屬性是一張**宣告式資料表**
//      （`ds::format::Value` Map，來自 E7-01 描述子系統），主題元件則以 E9-03 的可互換
//      組合（`ds::package::Composition`）表示——不重造資料模型。
//   2. **執行期切換當前主題**：`switch_to(name)` 切換當前主題並**通知相依元件套用新主題**
//      （透過變更回呼）。
//   3. **主題變更通知**：`on_theme_change(cb)` 註冊回呼；當前主題真正改變時觸發，帶新的
//      `ThemeData`，讓相依元件據以套用（顏色 / 樣式 / 可互換元件一併切換）。
//   4. **與 E7-07 熱重載整合**：`theme_from_document()` 把一份熱重載後的宣告式文件轉為主題資料；
//      `set_theme()` 於執行期以新資料更新既有主題——若更新的正是**當前主題**，會自動通知
//      （= 熱重載一份主題文件即時套用）。
//   5. **與 E9-03 可互換組合整合**：`ThemeData::components` 持有該主題的可互換元件組合；
//      切換主題即切換其元件集合，相依端於變更回呼中讀取新組合套用。
//
// 設計原則（與 E9-01 / E9-02 / E9-03 一致，命名空間同為 `ds::package`）：
//   - **平台中立、純邏輯**：不含任何 `#ifdef` / 系統呼叫 / 真實後端 / 平台分支。engine 層換平台一行不動。
//   - **查無主題不靜默**：切換 / 查詢未知主題一律**明確報錯**（回傳帶原因的 `ThemeResult`，或於
//     取當前主題而無當前主題時 `throw std::runtime_error`）——絕不安靜吞掉。
//   - **API 面無絕對座標 / 數字 z-order**（NFR-02）：主題屬性以不透明的宣告式 `Value` 承載，
//     本單元的 API 面不出現座標欄位或數字 z-order。
//   - 本單元不含能力閘控呼叫（純邏輯註冊 / 切換），無 `has()` 降級路徑需求（NFR-03 不適用）。
#ifndef DS_ENGINE_E9_04_THEME_HPP
#define DS_ENGINE_E9_04_THEME_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "composition.hpp"  // E9-03：ds::package::Composition（PUBLIC e9_03 傳遞 include）
#include "document.hpp"     // E7-01：ds::format::Value / Document（經 e7_07 → e7_01 傳遞）

namespace ds::package {

// 一個視覺主題的宣告式資料。純資料。
//   - name：主題穩定識別（註冊鍵，非空）。
//   - attributes：顏色 / 樣式 / 資源集的宣告式資料表（E7-01 `Value`，通常為 Map；
//     預設 Null = 尚無屬性）。語意對本單元不透明——本單元只搬運，不解讀欄位。
//   - components：該主題的可互換元件組合（E9-03 `Composition`），可為空組合。
//     切換主題即切換此組合，相依端據以套用主題元件。
struct ThemeData {
    std::string name;
    ds::format::Value attributes;  // 宣告式顏色 / 樣式 / 資源表（Map 或 Null=空）。
    Composition components;        // 可互換主題元件組合（E9-03），可為空。
};

// 註冊 / 切換 / 更新操作結果：成功，或帶不靜默的原因（如空名稱、重複、查無主題）。
struct ThemeResult {
    bool ok = false;
    std::string message;  // 失敗時的人類可讀原因；成功時為空。

    explicit operator bool() const noexcept { return ok; }

    static ThemeResult success() { return {true, {}}; }
    static ThemeResult failure(std::string why) { return {false, std::move(why)}; }
};

// 主題變更回呼：當前主題真正改變（或當前主題資料被更新）時觸發，帶新的 `ThemeData`。
using ThemeChangeCallback = std::function<void(const ThemeData&)>;

// 由一份熱重載 / 解析後的宣告式文件（E7-07 / E7-01）建立主題資料。
//   - name 為主題註冊名（呼叫端指定，須非空——空名於註冊時被拒）。
//   - attributes 取自 `doc.root`（該文件的內容 Map，不含 format_version 本身）。
// 這是與 E7-07 熱重載整合的橋接：HotReloader 交付新文件 → 轉為 ThemeData → set_theme() 套用。
ThemeData theme_from_document(std::string name, const ds::format::Document& doc);

// 主題管理器：註冊多主題、執行期切換當前主題、變更通知。
//   - 保留註冊順序以利穩定列舉。
//   - 平台中立、純邏輯；查無主題一律明確報錯不靜默。
class ThemeManager {
public:
    // 註冊一個**新**主題。name 須非空且未曾註冊。
    //   - name 為空 → failure；name 已存在 → failure（請改用 set_theme 更新）。
    //   - data.name 會被設為 name（以註冊鍵為準）。成功後不改變當前主題。
    ThemeResult register_theme(const std::string& name, ThemeData data);

    // 註冊或**更新**（upsert）一個主題：name 不存在則新增，存在則以新資料取代。
    //   - name 為空 → failure。
    //   - 若被更新者正是**當前主題** → 自動觸發變更通知（= 熱重載一份主題文件即時重新套用）。
    // 供 E7-07 熱重載整合：新文件 → theme_from_document → set_theme。
    ThemeResult set_theme(const std::string& name, ThemeData data);

    // 切換當前主題為 name。
    //   - name 未註冊 → failure(帶原因)，當前主題不變（不靜默）。
    //   - name 已是當前主題 → 視為 no-op success（不重複觸發通知）。
    //   - 成功切換到「不同」主題 → 更新當前主題並觸發變更通知。
    ThemeResult switch_to(const std::string& name);

    // 查詢：是否已註冊此主題。
    bool has_theme(const std::string& name) const noexcept;

    // 取得主題資料；未註冊 → nullptr（安全查詢）。
    const ThemeData* find_theme(const std::string& name) const noexcept;

    // 是否已選定當前主題。
    bool has_current() const noexcept { return current_ != kNone; }

    // 當前主題資料 / 名稱。無當前主題 → throw std::runtime_error（明確報錯，不回可疑預設值）。
    const ThemeData& current() const;
    const std::string& current_name() const;  // 便捷：current().name。

    // 依註冊順序列出所有已註冊主題名。
    std::vector<std::string> list_themes() const;

    // 已註冊主題數。
    std::size_t size() const noexcept { return themes_.size(); }

    // 註冊一個變更回呼（可註冊多個，依註冊順序於變更時依序觸發；傳空 std::function 會被略過）。
    void on_theme_change(ThemeChangeCallback cb);

private:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    // 以當前主題資料觸發所有已註冊回呼。僅在有當前主題時呼叫。
    void notify() const;

    // 線性查找（主題數通常很小；保留註冊順序以利穩定列舉）。回傳索引或 kNone。
    std::size_t index_of(const std::string& name) const noexcept;

    std::vector<ThemeData> themes_;               // 已註冊主題（保留註冊順序）。
    std::size_t current_ = kNone;                 // 當前主題索引；kNone = 尚未選定。
    std::vector<ThemeChangeCallback> callbacks_;  // 變更回呼。
};

}  // namespace ds::package

#endif  // DS_ENGINE_E9_04_THEME_HPP
