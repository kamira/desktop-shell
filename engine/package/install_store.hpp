// E9-10 已安裝套件登錄的持久化 — 把「已安裝了哪些套件」存成可重讀的宣告式紀錄
//                                （engine 層 / package 子系統 / 平台中立）
//
// 語意：E9-07 的 `InstallRegistry` 是**純記憶體**登錄（`drop_install.hpp` 檔內明寫「相位 1 的
// 安裝目標」），介面只有 `contains/add/find/size`——**重啟即失憶**。`CHG-20260829-05` 指名這個
// 缺口：下游（設定分頁依「安裝了哪些套件」而定、權限授予狀態）要的語意是「**有安裝即算在內，
// 即使從沒打開過**」，記憶體登錄撐不起來。E9-10 補的就是這一層：把已安裝套件的**中繼資料**
// 序列化為 E7-01 宣告式文字，存於可注入後端，日後**重新建構登錄物件即可重新列舉**——
// 全程不需要套件來源、不需要解析套件本體、不需要實例化任何東西。
//
// 「不載入也列得出」的物理基礎：
//   持久化的單位是 `InstallRecord`，而它持有的是 E9-01 的 `Package`——**manifest 中繼資料 +
//   內含項目的邏輯路徑清單**，本身不含任何資源內容。所以「列出已安裝套件」「讀出它宣告了哪些
//   requires / permissions」只需要這份紀錄，與套件本體是否被載入完全無關。
//
// 設計原則（承 E7-01 / E7-12，並與 E9-05 `layout_store` 同構）：
//   - **平台中立、純邏輯**：無任何 `#ifdef` / 平台分支 / 系統呼叫 / 真實檔案 I/O。持久化走
//     可注入的 `InstallStorage` 抽象後端，相位 1 預設實作為純記憶體 `MemoryInstallStorage`。
//     **誠實聲明：`MemoryInstallStorage` 是相位 1 的替身，真實磁碟後端留待後續相位**
//     （與 E9-05 `MemoryLayoutStorage` 同一姿態與同一誠實度）。
//   - **不重造格式**：序列化一律經 **E7-12 `serialize`** 產出 **E7-01** 宣告式文字，
//     反序列化一律經 **E7-01 `parse()`**；產出即 `parse()` 能再吃回去的合法輸入（round-trip）。
//     本紀錄格式自身帶版本欄位 `kInstallRegistryFormat`（Q3：格式從第一版就帶版本）。
//   - **不重造驗證**：紀錄寫入 / 讀回時的套件結構完整性一律交給 E9-01 `validate_package`。
//   - **不靜默失敗**（NFR-04 精神）：文字損毀 / 結構違反 / 缺必要欄位 / 文件內重名 →
//     一律 throw `std::runtime_error`（訊息可定位，解析錯誤附行號）；絕不回傳空清單掩蓋錯誤。
//
// 格式說明（E7-01 文法；**套件名以「清單項目的 name 欄位」承載，不當 map 鍵**）：
//     format_version: 1.0
//     packages:
//       -
//         name: com.example.hello
//         format_major: 1
//         format_minor: 0
//         version: 0.1.0            # 選填，空則不寫出
//         description: 範例模組      # 選填，空則不寫出
//         requires:                 # 選填清單
//           - host.tray_icon
//         permissions:
//           - fs.read
//         entries:                  # 內含項目清單（E9-01 Package.entries）
//           -
//             kind: asset
//             path: icons/tray.png
//   - **為何不用 `<package_name>:` 當 map 鍵**：E7-12 檔頭已記載「map 鍵不支援引號」（鍵為裸
//     token，不得含 `:` / 換行 / 前後空白）。套件名雖通常是 `com.example.hello` 這種安全形狀，
//     但 E9-02 對 name 只要求「非空」，並未禁止 `:`。把名字放在**值**的位置可完全規避此限制
//     （值經 E7-12 `needs_quote` 自動引號化 / 轉義，型別與內容不漂移）。
//   - **空容器的已知限制**：E7-01 中 `key:` 其下無更深縮排會解析為 Null，故空清單
//     （無套件 / 無 requires / 無 entries）再解析時塌為 Null。本實作**明確把 Null 與缺鍵一併
//     視為空清單**，並以測試釘住「空登錄 round-trip 仍為空登錄」。
//
// 相位 1 的「重啟」語意（驗收重點，不誇大）：
//   相位 1 無真實行程重啟可測。本單元把「重啟」定義為「**後端存活、登錄物件解構後重新建構**」：
//   `InstalledPackageStore` 的建構子即自後端重讀（rehydrate），故「新建一個 store 指向同一個
//   後端」就是重啟後的狀態。對同一個還活著的物件再查一次**不構成**任何證據，測試刻意不這麼寫。
//   相位 2 換上真實磁碟後端時：介面不動，只需實作 `InstallStorage`。
#ifndef DS_ENGINE_E9_10_INSTALL_STORE_HPP
#define DS_ENGINE_E9_10_INSTALL_STORE_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "document.hpp"      // E7-01：Value / Document / FormatVersion（經 e7_12 PUBLIC 傳遞）
#include "drop_install.hpp"  // E9-07：InstallRegistry（經其傳遞 E9-01 Package / E9-02 Manifest）
#include "writeback.hpp"     // E7-12：serialize（相依 target e7_12）

namespace ds::package {

// -----------------------------------------------------------------------------
// 已安裝紀錄：持久化的單位
// -----------------------------------------------------------------------------

// 單一已安裝套件的持久化紀錄。純資料、平台中立。
//
// 只承載**中繼資料**：E9-01 `Package` = E9-02 manifest + 內含項目的邏輯路徑清單。
// 不含任何資源內容 / 套件本體，故「列舉已安裝」永不需要載入套件（見檔首）。
struct InstallRecord {
    Package package;  // manifest（name / version / description / requires / permissions）+ entries

    // 套件識別（= manifest.name），持久化紀錄的鍵。
    const std::string& package_id() const noexcept { return package.manifest.name; }

    bool operator==(const InstallRecord& o) const;
    bool operator!=(const InstallRecord& o) const { return !(*this == o); }
};

// -----------------------------------------------------------------------------
// 序列化 / 反序列化：紀錄集 <-> 宣告式文字（透過 E7-01 / E7-12）
// -----------------------------------------------------------------------------

// 本持久化格式自身的版本欄位（寫入文件首行 `format_version`）。
// 註：與 E9-02 manifest 自身的 `format_version`（存於每筆紀錄的 format_major/minor）不同層級。
inline constexpr ds::format::FormatVersion kInstallRegistryFormat{1, 0};

// 將紀錄集序列化為 E7-01 宣告式文字（首行含 format_version）。使用 E7-12 `serialize`。
// 任一紀錄結構不合法（交由 E9-01 `validate_package`）或集合內重名 → throw std::runtime_error。
std::string serialize_install_records(const std::vector<InstallRecord>& records);

// 從宣告式文字反序列化回紀錄集。
// 解析失敗（附行號）/ 結構違反 / 缺 name / 文件內重名 / 套件結構不合法 → throw std::runtime_error。
// 空登錄（`packages:` 下無項目，E7-01 塌為 Null）→ 回傳空清單（非錯誤）。
std::vector<InstallRecord> deserialize_install_records(const std::string& text);

// -----------------------------------------------------------------------------
// 儲存後端：可注入的抽象（相位 1 = 純記憶體替身，無真實檔案 I/O）
// -----------------------------------------------------------------------------

// 已序列化登錄文字的鍵值儲存抽象（形狀同 E9-05 `LayoutStorage`）。
// 契約：以 slot 名稱為鍵，值為已序列化的登錄文字；put 覆寫既有；get 回傳是否存在。
class InstallStorage {
public:
    virtual ~InstallStorage() = default;

    // 寫入 / 覆寫 key 對應的登錄文字。
    virtual void put(const std::string& key, const std::string& text) = 0;

    // 讀取 key 對應的登錄文字；存在則寫入 out 並回 true，否則回 false
    //（不拋錯——「尚無任何登錄」是合法的初次狀態，由上層決定語意）。
    virtual bool get(const std::string& key, std::string& out) const = 0;

    // key 是否存在。
    virtual bool has(const std::string& key) const = 0;

    // 移除 key；存在並移除回 true，不存在回 false（不拋錯）。
    virtual bool erase(const std::string& key) = 0;

    // 列舉所有 slot 名稱（實作可自訂順序；MemoryInstallStorage 以字典序回傳，穩定可測）。
    virtual std::vector<std::string> names() const = 0;
};

// 純記憶體後端：**相位 1 的替身實作**，無真實檔案 I/O。名稱以字典序列舉（穩定）。
//
// 它之所以能支撐「重啟」語意：生命週期由呼叫端控制，可以**活得比登錄物件久**——
// 把它放在外層 scope、讓 `InstalledPackageStore` 在內層 scope 生滅，即為相位 1 的重啟模型。
// 真實磁碟後端留待後續相位（屆時本介面一行不動）。
class MemoryInstallStorage : public InstallStorage {
public:
    void put(const std::string& key, const std::string& text) override;
    bool get(const std::string& key, std::string& out) const override;
    bool has(const std::string& key) const override;
    bool erase(const std::string& key) override;
    std::vector<std::string> names() const override;

private:
    std::map<std::string, std::string> data_;  // 有序 map：列舉自然為字典序。
};

// -----------------------------------------------------------------------------
// 持久化的已安裝套件登錄
// -----------------------------------------------------------------------------

// 預設的儲存 slot 名稱。
inline constexpr const char* kDefaultInstallSlot = "installed";

// 已安裝套件登錄的持久化門面：**寫入即落盤（write-through）、建構即重讀（rehydrate）**。
//
//   - 建構子自後端讀取 slot 並反序列化 → 「新建一個 store 指向同一個後端」即重啟後的狀態。
//     後端尚無此 slot（初次啟動）→ 空登錄，**不是錯誤**。
//     後端有此 slot 但文字損毀 → throw（絕不靜默地當成空登錄，那會悄悄遺失所有安裝紀錄）。
//   - `record()` 更新記憶體索引後**立即**把整份登錄序列化寫回後端，故不存在「忘了存檔」的窗口。
//   - 不擁有後端生命週期（呼叫端須保證 storage 存活期涵蓋本物件使用期間）。
class InstalledPackageStore {
public:
    // 注入儲存後端與 slot 名稱；建構時即自後端重建登錄（見上）。
    // slot 為空 → throw std::runtime_error。後端文字損毀 → throw（由反序列化傳遞）。
    explicit InstalledPackageStore(InstallStorage& storage,
                                   std::string slot = kDefaultInstallSlot);

    // 登錄一個已安裝套件並**立即持久化**。
    //   - 同 package_id 已存在時**不覆寫**並回傳 false（與 E9-07 `InstallRegistry::add` 同語意；
    //     重複安裝由呼叫端明確處理），後端不被改寫。
    //   - manifest.name 為空、或套件結構不合法（E9-01 `validate_package`）→ throw。
    bool record(const Package& pkg);

    // 移除一筆已安裝紀錄並**立即持久化**。
    //   - 查無 package_id → 回傳 false（後端不變）。呼叫端若視為錯誤，自行判斷回傳值。
    //     （不拋錯是刻意的：E9-06 卸載流程已負責「查無此套件」的語意與回報。）
    bool forget(const std::string& package_id);

    // 是否已登錄此 package_id。
    bool contains(const std::string& package_id) const;

    // 取出某套件的持久化中繼資料；不存在回 nullptr。**不載入套件本體。**
    const Package* find(const std::string& package_id) const;

    // 列舉所有已安裝套件的 package_id（字典序，穩定可測）。**不載入套件本體。**
    std::vector<std::string> list() const;

    // 所有紀錄（字典序，依 package_id）。**不載入套件本體。**
    std::vector<InstallRecord> records() const;

    // 目前已登錄套件數。
    std::size_t size() const noexcept { return by_id_.size(); }

    // 自後端重讀並取代記憶體索引（捨棄未落盤的狀態；write-through 下本就不應存在）。
    // 用於「後端被外部改寫」的情境；文字損毀 → throw。
    void reload();

    // 目前持久化後的登錄文字（E7-01 宣告式格式；供查驗 / 測試 round-trip）。
    std::string serialized() const;

private:
    void flush();  // 把目前索引整份序列化寫回後端。

    InstallStorage& storage_;
    std::string slot_;
    std::map<std::string, InstallRecord> by_id_;  // 有序 map：列舉自然為字典序。
};

// 把持久化登錄的紀錄重播進一個 E9-07 `InstallRegistry`，回傳實際新增的筆數。
//
// 這是「重啟後回填」的接點：重啟後以存活的後端建構 `InstalledPackageStore`，再把它
// `mirror_into` 一個**全新的** `InstallRegistry`——過程中不需要任何 `InstallSource`、
// 不需要 `DropInstaller`、不解析任何套件描述文字。
// `InstallRegistry::add` 對同名不覆寫，故 registry 內已存在的 package_id 不計入回傳值。
// E9-07 已合併、可讀不可改：本函式只呼叫其公開 `add`。
std::size_t mirror_into(const InstalledPackageStore& store, InstallRegistry& registry);

}  // namespace ds::package

#endif  // DS_ENGINE_E9_10_INSTALL_STORE_HPP
