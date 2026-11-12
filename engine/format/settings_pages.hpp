// E7-16 設定分頁登錄（核心分頁 + 每套件一頁，append-only）
//                （engine 層 / 描述子系統 / 平台中立 / 相依 E7-14 圖形化設定與就地編輯、E9-10 已安裝套件登錄）
//
// 語意：設定面板由**多個分頁**組成——一個**核心分頁**（平台自身的設定），加上
// **每個已安裝套件各一頁**。套件的貢獻是 **append-only**：
//
//     「這裡的由已安裝貢獻**不是複寫**，而是**額外增加**設定的 tab」
//      —— 使用者於 `CHG-20260829-05` 的明確裁定。
//
// 這條裁定是本單元的核心契約，而它的危險處在於**正向測試證明不了它**：
// 看到「套件分頁出現了」只說明 append 成立，沒有說明 **not-overwrite** 成立。
// 唯一能釘住它的是**衝突情境下的反向斷言**：讓套件宣告一個與核心設定**同名**的欄位，
// 然後斷言核心分頁的值**原封不動**。本單元的契約測試即以此為主軸（見 `tests/e7/test_e7_16.cpp`）。
//
// append-only 的機械保證（不靠紀律，靠結構）：
//   1. **核心分頁的欄位只由核心 `Value` 樹算出**。套件貢獻一律產生**另一個** `SettingsPage`
//      物件，持有**自己的** `Value` 樹；因此套件鍵名撞上核心鍵名時，**不存在任何寫入核心的
//      程式路徑**——不是「檢查後拒絕覆寫」，是**連覆寫的管道都不存在**。
//   2. `refresh()` 只重建套件分頁集合，**完全不碰** 核心分頁（連重算都不做）。
//   3. 核心分頁的 page id（`kCorePageId`）是**保留字**：貢獻來源若試圖佔用它 → `throw`
//      （絕不靜默地讓它取代核心分頁）。
//   4. 列舉順序**固定**：核心分頁恆為第一，套件分頁依 package id 字典序。
//      順序固定才使衝突**無法被排序遮蔽**（先到先贏 / 後到覆蓋這類隱性語意無處藏身）。
//
// 不重造欄位模型（驗收標準明文要求）：
//   每一分頁的欄位清單一律是 **E7-14 `EditableField`** 的 `std::vector`，由 E7-14
//   `build_editor_model` 產出；分頁上的編輯一律經 E7-14 `apply_edit`（其內部再經 E7-12
//   `set_value`）。本單元**不新增任何欄位型別、不自造控制項型別、不自造驗證規則**。
//
// 為什麼「哪些套件貢獻了設定」是**可注入抽象**，而不是直接連 E9-10：
//   E9-10 `InstalledPackageStore` 回答的是「安裝了哪些套件」，其持久化單位 `InstallRecord`
//   持有 E9-01 `Package` = E9-02 manifest（`name` / `version` / `description` / `requires` /
//   `permissions`）+ 內含項目的**邏輯路徑清單**。而 E9-02 `Manifest` **沒有任何設定宣告欄位**，
//   E9-01 `Package` 檔頭亦明寫其**不含套件本體 / 資源內容**。
//   也就是說「這個套件宣告了哪些設定欄位」這份資料**不在 E9-10 的回答範圍內**——它在套件隨附的
//   宣告式設定資源（E7-01 文字）裡。故即使直接連結 e9_10，仍須另有一層把那份內容交進來。
//   本單元因此定義 `SettingsContributionSource`：回答「哪些 package id 有貢獻 + 其宣告的設定
//   `Value` 樹」。這也與本 repo 既有風格一致（E9-05 `LayoutStorage`、E9-10 `InstallStorage`、
//   E2-19 `ProcessSource` 皆為可注入抽象），且使本單元不需跨子系統連結、測試不需真的建一個
//   安裝登錄。host 層組裝時才決定來源：屆時由 E9-10 列舉已安裝套件，再取其設定資源內容餵進來。
//
// 誠實分界（沿用 E9-10 `MemoryInstallStorage` 的同一誠實度，不誇大）：
//   `MemorySettingsContributions` 是**相位 1 的記憶體替身**——無真實檔案 I/O、無跨行程持久化。
//   本單元**不聲稱**設定分頁跨真實行程重啟仍然存在。本單元所稱的「卸載」是
//   「貢獻來源不再列出該 package id」，`refresh()` 後該分頁即消失。
//   真實的安裝 / 卸載落盤由 E9-10（及其後續相位的真實磁碟後端）負責。
//
// 設計原則（承 E7-01 / E7-12 / E7-14）：
//   - **平台中立、純邏輯**：無任何 `#ifdef` / 平台分支 / 系統呼叫 / 真實檔案 I/O / GUI 後端。
//     engine 層換平台一行不動（本單元是分頁的**資料模型**，不是真實 GUI）。
//   - **不靜默失敗**（NFR-04 精神）：保留 page id 被佔用、貢獻 id 為空、重複 id、宣告的設定根
//     不是 Map、來源列出了 id 卻取不到貢獻、對不存在的分頁 / 欄位編輯 —— 一律
//     `throw std::runtime_error`（訊息可定位到 page id / 欄位路徑），絕不回傳可疑的預設值。
#ifndef DS_ENGINE_E7_16_SETTINGS_PAGES_HPP
#define DS_ENGINE_E7_16_SETTINGS_PAGES_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "editor_model.hpp"  // E7-14：EditableField / EditorSchema / build_editor_model / apply_edit
                             //（相依 target e7_14；E7-12 Path / set_value 與 E7-01 Value / Document
                             // 經其 PUBLIC 傳遞相依可見）

namespace ds::format {

// -----------------------------------------------------------------------------
// 分頁來源
// -----------------------------------------------------------------------------

// 一個分頁從何而來。決定它是否可被 `refresh()` 重建 / 移除（核心分頁永不被動）。
enum class PageOrigin {
    Core,     // 平台自身的核心設定分頁；恆存在，永不被套件貢獻改動或移除。
    Package,  // 由某個已安裝套件貢獻的分頁；套件卸載後消失。
};

// 分頁來源的人類可讀名（供訊息 / 測試；不會回傳 nullptr）。
const char* to_string(PageOrigin origin) noexcept;

// 核心分頁的保留 page id。**套件不得佔用**（佔用 → throw）。
inline constexpr const char* kCorePageId = "core";

// 核心分頁的預設標題（呼叫端可於建構時覆寫）。
inline constexpr const char* kCorePageTitle = "核心設定";

// -----------------------------------------------------------------------------
// 一個設定分頁
// -----------------------------------------------------------------------------

// 單一設定分頁：一棵設定 `Value` 樹 + 由它算出的 E7-14 可編輯欄位清單。
//
// `values` 是**唯一事實來源**，`fields` 恆為「由 `values` 經 E7-14 `build_editor_model`
// 算出的結果」。分頁上的每次編輯都會更新 `values` 並**重算** `fields`，故二者不會漂移。
struct SettingsPage {
    std::string id;                 // 核心 = `kCorePageId`；套件分頁 = 該套件的 package id。
    std::string title;              // 人類可讀標題。
    PageOrigin origin = PageOrigin::Core;
    std::string owner_package_id;   // 貢獻此分頁的套件 id；**核心分頁為空字串**。
    Value values;                   // 該分頁背後的設定樹（恆為 Map 型別）。
    std::vector<EditableField> fields;  // E7-14 欄位模型（**不重造**）；由 values 算出。
    EditorSchema schema;            // 建模用的 E7-14 欄位提示（可為空）。

    // 依路徑字串取本分頁的欄位（未命中回 nullptr）。轉呼 E7-14 `find_field`。
    const EditableField* field(const std::string& path_text) const;

    // 本分頁的欄位數。
    std::size_t field_count() const noexcept { return fields.size(); }
};

// -----------------------------------------------------------------------------
// 貢獻：一個套件宣告的設定分頁內容
// -----------------------------------------------------------------------------

// 單一套件對設定面板的貢獻。純資料、平台中立。
//
// 註：`declared` 是該套件**宣告的設定樹**（E7-01 `Value`，須為 Map）。它來自套件隨附的
// 宣告式設定資源，**不是** E9-02 manifest 的欄位（manifest 不含設定宣告，見檔首）。
struct SettingsContribution {
    std::string package_id;  // 貢獻者的套件 id（= E9-02 `Manifest::name`）；非空，且不得為 `kCorePageId`。
    std::string title;       // 分頁標題；空字串 → 以 package_id 當標題。
    Value declared;          // 宣告的設定樹（須為 Map；空 Map 合法 → 該分頁零欄位）。
    EditorSchema schema;     // 選填：E7-14 欄位提示（Enum / 數值範圍 / 自訂標籤）。
};

// 「哪些套件貢獻了設定分頁」的可注入抽象後端（形狀同 E9-10 `InstallStorage`）。
//
// 契約：
//   - `contributing_package_ids()` 列出目前有貢獻的 package id。實作可自訂順序——
//     `SettingsPageRegistry` 一律重新以字典序排列，故順序不影響結果（刻意如此：
//     來源的列舉順序**不得**成為衝突語意的一部分）。清單內不得有重複 id。
//   - `contribution(id, out)`：命中則寫入 out 並回 true，否則回 false。
//     「列出了某 id 卻取不到它的貢獻」是來源自身不一致，由 registry 明確 throw（不靜默略過）。
class SettingsContributionSource {
public:
    virtual ~SettingsContributionSource() = default;

    // 目前有貢獻的 package id 清單（順序不拘；不得重複）。
    virtual std::vector<std::string> contributing_package_ids() const = 0;

    // 取某套件的貢獻；存在則寫入 out 並回 true，否則回 false（不拋錯——
    // 「這個套件沒有貢獻設定」是合法狀態，由上層決定語意）。
    virtual bool contribution(const std::string& package_id, SettingsContribution& out) const = 0;
};

// 純記憶體貢獻來源：**相位 1 的替身實作**，無真實檔案 I/O、無跨行程持久化（見檔首誠實分界）。
// id 以字典序列舉（穩定可測）。
class MemorySettingsContributions : public SettingsContributionSource {
public:
    // 登錄 / 覆寫一個套件的貢獻（同 package_id 覆寫既有——同一套件重新宣告自己的設定是合法的；
    // append-only 約束的是「**套件不得改動核心分頁**」，不是「套件不得改自己那一頁」）。
    //   - package_id 為空、或等於 `kCorePageId`（保留字）、或 declared 非 Map → throw std::runtime_error。
    void declare(SettingsContribution contribution);

    // 便捷多載：以 package_id + 宣告樹登錄（標題取 package_id，無 schema）。
    void declare(const std::string& package_id, Value declared);

    // 卸載：移除某套件的貢獻。存在並移除回 true，不存在回 false（不拋錯）。
    bool withdraw(const std::string& package_id);

    // 移除所有貢獻。
    void clear();

    // 目前貢獻數。
    std::size_t size() const noexcept { return by_id_.size(); }

    std::vector<std::string> contributing_package_ids() const override;
    bool contribution(const std::string& package_id, SettingsContribution& out) const override;

private:
    std::map<std::string, SettingsContribution> by_id_;  // 有序 map：列舉自然為字典序。
};

// -----------------------------------------------------------------------------
// 設定分頁登錄
// -----------------------------------------------------------------------------

// 設定分頁登錄：核心分頁 + 每套件一頁，**append-only**。
//
// 生命週期：建構時即建立核心分頁並自來源建立套件分頁（等同一次 `refresh()`）。
// 之後套件安裝 / 卸載反映到來源後，呼叫 `refresh()` 重建**套件分頁集合**；
// **核心分頁在 `refresh()` 中完全不被觸碰**（連重算都不做），這就是 append-only 的結構保證。
//
// 不擁有來源的生命週期（呼叫端須保證 source 存活期涵蓋本物件使用期間，同 E9-10 的姿態）。
class SettingsPageRegistry {
public:
    // 以核心設定樹 + 貢獻來源建構（核心標題取 `kCorePageTitle`，核心無 schema）。
    // core_values 非 Map → throw std::runtime_error。
    SettingsPageRegistry(Value core_values, SettingsContributionSource& source);

    // 同上，另給核心分頁的 E7-14 欄位提示與標題（title 為空 → 用 `kCorePageTitle`）。
    SettingsPageRegistry(Value core_values,
                         EditorSchema core_schema,
                         SettingsContributionSource& source,
                         std::string core_title = std::string());

    // 自來源重建**套件分頁集合**；核心分頁不受影響（不重算、不改值）。
    //
    // throw std::runtime_error 的情形（絕不靜默）：
    //   - 來源列出的 id 為空字串；
    //   - 來源列出的 id 等於 `kCorePageId`（保留字；**套件不得取代核心分頁**）；
    //   - 來源列出重複的 id；
    //   - 來源列出了某 id，`contribution()` 卻回 false（來源自身不一致）；
    //   - 某貢獻的 `declared` 非 Map 型別。
    // 任一情形下**登錄維持原狀**（先全部建好才整批替換，不留半套狀態）。
    void refresh();

    // 分頁總數（核心分頁 + 套件分頁）；恆 >= 1。
    std::size_t size() const noexcept { return 1 + package_pages_.size(); }

    // 套件分頁數（不含核心分頁）。
    std::size_t package_page_count() const noexcept { return package_pages_.size(); }

    // 所有 page id：**核心分頁恆為第一**，其後為套件分頁依字典序（穩定可測；見檔首保證 4）。
    std::vector<std::string> page_ids() const;

    // 所有分頁，順序同 `page_ids()`。
    std::vector<SettingsPage> pages() const;

    // 核心分頁（恆存在）。
    const SettingsPage& core_page() const noexcept { return core_; }

    // 依 page id 取分頁（`kCorePageId` 取核心分頁）；未命中回 nullptr。
    const SettingsPage* page(const std::string& page_id) const;

    // 是否存在此 page id。
    bool contains(const std::string& page_id) const;

    // 核心分頁的設定樹（唯一事實來源）。
    const Value& core_values() const noexcept { return core_.values; }

    // 某套件分頁的設定樹；該套件無分頁回 nullptr。
    const Value* package_values(const std::string& package_id) const;

    // 對某分頁的某欄位套用編輯：一律經 **E7-14 `apply_edit`**（型別 / 約束驗證 + E7-12 寫回），
    // 通過後更新該分頁的 `values` 並**重算** `fields`。
    //
    // **分頁間互不影響**：對核心分頁的編輯不動任何套件分頁，反之亦然。
    // throw std::runtime_error（絕不靜默）：page id 不存在、該分頁無此欄位路徑、
    // 或 E7-14 驗證不通過（型別不符 / 越界 / 非列舉選項）。驗證失敗時該分頁**不被改動**。
    void apply_page_edit(const std::string& page_id,
                         const std::string& path_text,
                         Value new_value);

private:
    // 由一棵設定樹 + schema 建一個分頁（欄位經 E7-14 build_editor_model 算出）。
    static SettingsPage make_page(std::string id,
                                  std::string title,
                                  PageOrigin origin,
                                  std::string owner_package_id,
                                  Value values,
                                  EditorSchema schema);

    SettingsPage core_;                                // 核心分頁；refresh() 不觸碰。
    std::map<std::string, SettingsPage> package_pages_;  // 有序 map：列舉自然為字典序。
    SettingsContributionSource& source_;
};

}  // namespace ds::format

#endif  // DS_ENGINE_E7_16_SETTINGS_PAGES_HPP
