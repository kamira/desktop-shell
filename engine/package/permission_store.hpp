// E9-11 權限授予狀態與決策記錄 — 每套件每權限三態 + 持久化 + 投影成 E8-04 HostEnvironment
//                                （engine 層 / package 子系統 / 平台中立）
//
// 語意：E9-02 manifest 宣告一個套件**想要**哪些權限（`permissions`），E8-04 載入時據
// `HostEnvironment` 判斷 host **是否授予**（未授予 → `MissingPermission`，附缺項）。中間缺的那
// 一層就是本單元：**誰在什麼時候對哪個套件的哪個權限做了什麼決定，而這個決定要活過重啟。**
//
// 設計原則（承 E9-10 / E9-05，不另闢蹊徑）：
//   - **平台中立、純邏輯**：無任何 `#ifdef` / 平台分支 / 系統呼叫 / 真實檔案 I/O。
//   - **不重造格式**：序列化一律經 **E7-12 `serialize`** 產出 **E7-01** 宣告式文字，
//     反序列化一律經 **E7-01 `parse()`**；本紀錄格式自身帶版本欄位（Q3）。
//   - **不重造後端抽象**：持久化後端**直接沿用 E9-10 的 `InstallStorage`**（見下方 alias），
//     不新增第三個形狀一模一樣的抽象。相位 1 替身為 `MemoryInstallStorage`。
//   - **不靜默失敗 / 不靜默降級**：見下方「三態 → 兩態的有損投影」與「不靜默降級」兩節。
//
// ---------------------------------------------------------------------------
// 為什麼是三態，而不是一個 bool
// ---------------------------------------------------------------------------
// `PermissionDecision` 是**具名三態枚舉**（刻意不是 `bool`、也不是 `std::optional<bool>`）。
// 三態的意義**不在載入閘控**（未決與已拒絕在閘控上行為相同，都擋），而在**決策流程**：
// 使用者在 `CHG-20260829-05` 裁定「Q-C 權限在安裝時問」，那就必須分得出
//   - **未決**（`Undecided`）= 從沒問過 → **該問**；
//   - **已拒絕**（`Denied`）= 問過被拒 → **不要再問**，且擋載入。
// 若用一個 bool 表示，這兩者會塌成同一個狀態，那條裁定就實作不出來。
// 故本單元在**查詢介面**上保證二者可區分（`decision()` 直接回三態之一；
// `undecided_permissions()` 與 `denied_permissions()` 是兩個不同的清單），並有測試釘住。
//
// 「未決」= **沒有紀錄**（而非一筆值為 Undecided 的紀錄）。`revoke()` 即移除紀錄 → 回到未決，
// 代表「這個決定被撤回了，日後可以再問」；`deny()` 則留下一筆 Denied → 不要再問。
//
// ---------------------------------------------------------------------------
// 三態 → E8-04 兩態的有損投影（方向決定對錯）
// ---------------------------------------------------------------------------
// E8-04 的 `HostEnvironment`（`engine/script/module_loader.hpp`）只有兩態：某權限**在**
// `permissions_` 集合裡，或**不在**（檔頭明寫「空集合 = 什麼都不提供 / 不授予」）。它沒有
// deny / revoke / 任何第三態，而**本單元不得修改 E8-04**（驗收標準明文）。因此三態活在本單元
// 的紀錄裡，再投影過去：
//
//     已授予 Granted    →  grant_permission(id)   （進集合）
//     未決   Undecided  →  不呼叫                  （不進集合）
//     已拒絕 Denied     →  不呼叫                  （不進集合）
//
// 這個投影**刻意有損**，而有損的方向是唯一正確的方向：未決與已拒絕都**不授予**，載入時
// E8-04 回 `MissingPermission` —— 兩者在閘控上同行為，沒有問題。
// **嚴禁相反方向**：把「未決」當成「預設授予」（樂觀預設）。那會讓一個從未被問過的權限直接
// 通行，正是驗收標準禁止的靜默降級，只是方向相反。
//
// 為了讓「有損」不等於「看不見」，`evaluate_permissions()` 回傳的 `PermissionGate` 會把被扣住
// 的權限**按原因分成兩個清單**（`undecided` / `denied`），呼叫端因此知道該去問哪些、
// 哪些不要再問 —— 投影有損，但資訊沒有被丟掉，只是不在 `HostEnvironment` 裡。
//
// ---------------------------------------------------------------------------
// 不靜默降級（驗收標準明文）
// ---------------------------------------------------------------------------
// 「不得靜默降級成載入成功但功能沒作用」有兩個方向，兩邊都要守：
//   1. **被擋時要擋得明白**：權限不足 → E8-04 回 `MissingPermission` 且 `missing` 附缺項。
//      本單元保證 `PermissionGate::withheld` 與 E8-04 `LoadResult::missing` 按同一順序
//      （manifest 宣告順序）列出同一組權限，呼叫端不必猜缺什麼（精確範圍見 `withheld` 註解）。
//   2. **沒被擋時功能要真的在**：本單元不提供任何「部分授權就讓它載入」的路徑——
//      `PermissionGate::loadable` 為真 iff 該套件 manifest 宣告的**每一項**權限都是 `Granted`。
//      不存在「載入成功但某權限其實沒授予」的中間狀態。
//
// ---------------------------------------------------------------------------
// 持久化格式（E7-01 文法；**套件名 / 權限名皆放在「值」的位置，不當 map 鍵**）
// ---------------------------------------------------------------------------
//     format_version: 1.0
//     decisions:
//       -
//         package: com.example.hello
//         permission: fs.read
//         decision: granted        # granted | denied
//
//   - **為何不用 `<package>:` / `<permission>:` 當 map 鍵**：E7-12 檔頭記載「map 鍵不支援引號」
//     （鍵為裸 token，不得含 `:` / 換行 / 前後空白）。E9-02 對 `name` 只要求非空、未禁止 `:`，
//     權限 id 同理。把兩者放在**值**的位置可完全規避此限制（值經 E7-12 `needs_quote` 自動
//     引號化 / 轉義，型別與內容不漂移）。與 E9-10 同一處理。
//   - **空容器的已知限制**：E7-01 中 `key:` 其下無更深縮排會解析為 Null，故空清單再解析時塌為
//     Null。讀取端**把 Null 與缺鍵一併視為空**，並以測試釘住「空紀錄 round-trip 仍為空」。
//   - **未決不被寫出**：未決 = 沒有紀錄，故序列化只寫 `granted` / `denied` 兩種。讀取端對字面
//     `undecided` 的紀錄**明確接受並視為沒有決定**（人工編輯過的檔案可能出現；它不帶任何資訊），
//     對**其他**任何字面值則 throw（不靜默當成某一態）。
//
// ---------------------------------------------------------------------------
// 相位 1 的「重啟」語意（驗收重點，不誇大；與 E9-10 同一誠實度）
// ---------------------------------------------------------------------------
// 相位 1 無真實行程重啟可測，且 engine 層不得有真實檔案 I/O（G1b / `PHASE-PLAN.md`），故後端是
// **相位 1 的記憶體替身**（`MemoryInstallStorage`，E9-10 既有）。「重啟」定義為
// 「**後端存活、狀態物件解構後重新建構**」：`PermissionLedger` **建構即自後端重讀**
// （rehydrate）、每次決定**寫入即落盤**（write-through，**`revoke()` 也落盤**——否則
// 「撤銷 → 重啟 → 又變成已授予」就是另一種靜默降級），所以「新 ledger 指向同一個存活後端」
// 在資訊流上等價於重啟後：唯一的資訊通道是那份已序列化的文字。
// **刻意不寫**「對同一個還活著的 ledger 再查一次」——那證明不了任何事。
#ifndef DS_ENGINE_E9_11_PERMISSION_STORE_HPP
#define DS_ENGINE_E9_11_PERMISSION_STORE_HPP

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "document.hpp"       // E7-01：Value / Document / FormatVersion（經 e7_12 PUBLIC 傳遞）
#include "install_store.hpp"  // E9-10：InstallStorage / MemoryInstallStorage / InstalledPackageStore
#include "module_loader.hpp"  // E8-04：HostEnvironment（**只讀不改**，相依 target e8_04）
#include "package.hpp"        // E9-01：Package（經 e9_10 → e9_07 傳遞；manifest.permissions 來源）

namespace ds::package {

// -----------------------------------------------------------------------------
// 三態
// -----------------------------------------------------------------------------

// 某套件的某權限處於哪一態。**具名三態**，不是 bool、不是 optional<bool>（理由見檔首）。
enum class PermissionDecision {
    Undecided,  // 從沒問過 → 該問。不授予（擋載入）。
    Granted,    // 已授予 → 授予。
    Denied,     // 問過被拒 → 不要再問。不授予（擋載入）。
};

// 枚舉 ↔ 持久化字面值（"undecided" / "granted" / "denied"）。
const char* to_string(PermissionDecision decision) noexcept;

// 由字面值解析回枚舉；無法辨識回 false（呼叫端須明確處理，不得靜默當成某一態）。
bool parse_permission_decision(const std::string& text, PermissionDecision& out) noexcept;

// -----------------------------------------------------------------------------
// 一筆決策紀錄：持久化的單位
// -----------------------------------------------------------------------------

// (套件, 權限) → 決定。純資料、平台中立。
// `decision` 恆為 `Granted` 或 `Denied`：**未決不是一筆紀錄，而是沒有紀錄**（見檔首）。
struct PermissionRecord {
    std::string package_id;
    std::string permission_id;
    PermissionDecision decision = PermissionDecision::Undecided;

    bool operator==(const PermissionRecord& o) const noexcept {
        return package_id == o.package_id && permission_id == o.permission_id &&
               decision == o.decision;
    }
    bool operator!=(const PermissionRecord& o) const noexcept { return !(*this == o); }
};

// -----------------------------------------------------------------------------
// 序列化 / 反序列化：紀錄集 <-> 宣告式文字（透過 E7-01 / E7-12）
// -----------------------------------------------------------------------------

// 本持久化格式**自身**的版本欄位（寫入文件首行 `format_version`）。
// 註：與 E9-02 manifest 自身的 format_version、與 E9-10 的 kInstallRegistryFormat 皆不同層級。
inline constexpr ds::format::FormatVersion kPermissionLedgerFormat{1, 0};

// 將紀錄集序列化為 E7-01 宣告式文字（首行含 format_version）。使用 E7-12 `serialize`。
// 空 package_id / 空 permission_id / `decision == Undecided`（未決不得被寫出）/
// 集合內重複的 (package, permission) → throw std::runtime_error。
std::string serialize_permission_records(const std::vector<PermissionRecord>& records);

// 從宣告式文字反序列化回紀錄集。
// 解析失敗（附行號）/ 根節點非 Map / `decisions` 非清單 / 項目非 Map / 缺必要欄位 /
// 欄位型別不符 / 空 id / 無法辨識的 decision 字面值 / 重複的 (package, permission)
// → throw std::runtime_error。
// 空清單（E7-01 塌為 Null）→ 空紀錄集（非錯誤）。
// 字面 `undecided` 的紀錄被接受並**略過**（它不帶任何資訊；見檔首）。
std::vector<PermissionRecord> deserialize_permission_records(const std::string& text);

// -----------------------------------------------------------------------------
// 儲存後端：沿用 E9-10 的可注入抽象（不新增第三個同形狀抽象）
// -----------------------------------------------------------------------------

// 本單元需要的後端契約與 E9-10 `InstallStorage` **完全相同**（以 slot 名稱為鍵的序列化文字
// 鍵值儲存），故直接沿用，僅取一個讀得懂的別名。相位 2 換上真實磁碟後端時，E9-10 與本單元
// 共用同一個實作，介面一行不動。
using PermissionStorage = InstallStorage;
using MemoryPermissionStorage = MemoryInstallStorage;

// 預設的儲存 slot 名稱（與 E9-10 的 `"installed"` 不同 slot，互不干擾）。
inline constexpr const char* kDefaultPermissionSlot = "permissions";

// -----------------------------------------------------------------------------
// 權限決策帳本
// -----------------------------------------------------------------------------

// 每套件每權限三態的持久化帳本：**建構即自後端重讀（rehydrate）、每次決定寫入即落盤
// （write-through，撤銷亦然）**。
//
//   - 建構子自後端讀取 slot 並反序列化 → 「新建一個 ledger 指向同一個後端」即重啟後的狀態。
//     後端尚無此 slot（初次啟動）→ 空帳本，**不是錯誤**。
//     後端有此 slot 但文字損毀 → throw（絕不靜默當成空帳本——那會把所有「已拒絕」悄悄
//     變回「未決」，下一次安裝流程就會重新詢問已經被拒絕過的權限）。
//   - 不擁有後端生命週期（呼叫端須保證 storage 存活期涵蓋本物件使用期間）。
class PermissionLedger {
public:
    // 注入儲存後端與 slot 名稱；建構時即自後端重建帳本（見上）。
    // slot 為空 → throw std::runtime_error。後端文字損毀 → throw（由反序列化傳遞）。
    explicit PermissionLedger(PermissionStorage& storage,
                              std::string slot = kDefaultPermissionSlot);

    // --- 查詢（三態在此可區分）-------------------------------------------------

    // 某套件某權限的狀態。**查無紀錄 = `Undecided`**（而非 `Denied`：二者語意不同，見檔首）。
    PermissionDecision decision(const std::string& package_id,
                                const std::string& permission_id) const;

    // 是否已經做過決定（`Granted` 或 `Denied`）。`Undecided` → false。
    // 這是「該不該問使用者」的直接依據（Q-C：權限在安裝時問）。
    bool is_decided(const std::string& package_id, const std::string& permission_id) const;

    // --- 決定（每一個都立即落盤）-----------------------------------------------

    // 授予。回傳狀態是否真的改變（本來就是 Granted → false，後端不被改寫）。
    // 空 package_id / 空 permission_id → throw std::runtime_error。
    bool grant(const std::string& package_id, const std::string& permission_id);

    // 拒絕（= 問過被拒，不要再問）。回傳狀態是否真的改變。
    bool deny(const std::string& package_id, const std::string& permission_id);

    // 撤銷既有決定 → 回到 `Undecided`（日後可以再問）。回傳是否確有決定被撤銷。
    // **撤銷同樣立即落盤**：否則「已授予 → 撤銷 → 重啟 → 又變成已授予」。
    bool revoke(const std::string& package_id, const std::string& permission_id);

    // 撤銷某套件的**全部**決定（卸載套件時用），回傳被撤銷的筆數；>0 即落盤。
    std::size_t revoke_package(const std::string& package_id);

    // --- 內省 ------------------------------------------------------------------

    // 目前的決策筆數（未決不計——它不是一筆紀錄）。
    std::size_t size() const noexcept { return by_key_.size(); }

    // 全部紀錄，依 (package_id, permission_id) 字典序（穩定可測）。
    std::vector<PermissionRecord> records() const;

    // 某套件的全部紀錄，依 permission_id 字典序。
    std::vector<PermissionRecord> records_for(const std::string& package_id) const;

    // 有決策紀錄的套件 id（字典序，去重）。
    std::vector<std::string> packages() const;

    // 自後端重讀並取代記憶體索引（捨棄未落盤狀態；write-through 下本不應存在）。
    // 用於「後端被外部改寫」的情境；文字損毀 → throw。
    void reload();

    // 目前持久化後的帳本文字（E7-01 宣告式格式；供查驗 / 測試 round-trip）。
    std::string serialized() const;

private:
    using Key = std::pair<std::string, std::string>;  // (package_id, permission_id)

    void flush();  // 把目前索引整份序列化寫回後端。
    bool set(const std::string& package_id, const std::string& permission_id,
             PermissionDecision decision);

    PermissionStorage& storage_;
    std::string slot_;
    // 有序 map：列舉自然為 (package, permission) 字典序。只存 Granted / Denied。
    std::map<Key, PermissionDecision> by_key_;
};

// -----------------------------------------------------------------------------
// 閘門評估：把「有損投影」顯式化
// -----------------------------------------------------------------------------

// 對一個套件做權限閘門評估的結果。
//
// `undecided` 與 `denied` 都是**會擋住載入**的缺項，但分開列出是刻意的：前者該去問使用者，
// 後者不要再問（見檔首「為什麼是三態」）。各清單皆依 **manifest 宣告順序**（E8-04 亦按宣告
// 順序收集 `missing`），精確的同序範圍見下方 `withheld` 的說明。
struct PermissionGate {
    // 該套件 manifest 宣告的每一項權限都是 `Granted` → true。
    // **不存在「部分授權也可載入」的路徑**（不靜默降級）。
    bool loadable = false;

    std::vector<std::string> granted;    // 已授予（宣告順序）
    std::vector<std::string> undecided;  // 未決 → 該問（宣告順序）
    std::vector<std::string> denied;     // 已拒絕 → 不要再問（宣告順序）

    // 被扣住的權限，依 manifest 宣告順序（= 未決 ∪ 已拒絕，**非** undecided 接 denied）。
    // 這就是 E8-04 載入時會回在 `LoadResult::missing` 裡的那份清單。
    //
    // **同序的精確範圍**：manifest 未重複宣告同一權限時，`withheld` 與 E8-04 的 `missing`
    // **逐項相等**（含順序）。manifest 重複宣告時 `withheld` 為去重後的形式，而 E8-04 的
    // `missing` 不去重，故此時兩者為「同序但 E8-04 多出重複項」的關係——兩種情形皆有測試釘住。
    std::vector<std::string> withheld;

    // 是否有任何權限尚未被問過（呼叫端據此決定要不要跳出詢問流程）。
    bool needs_prompt() const noexcept { return !undecided.empty(); }
};

// 評估一個套件（依其 manifest.permissions）在此帳本下的閘門狀態。
// manifest 內重複宣告同一權限 → 在各清單中僅出現一次（去重，保留首次出現的順序）。
PermissionGate evaluate_permissions(const PermissionLedger& ledger, const Package& pkg);

// 同上，但直接給權限清單（供不持有 Package 的呼叫端 / 測試使用）。
PermissionGate evaluate_permissions(const PermissionLedger& ledger,
                                    const std::string& package_id,
                                    const std::vector<std::string>& permissions);

// -----------------------------------------------------------------------------
// 投影：組出 E8-04 HostEnvironment（**不修改 E8-04**）
// -----------------------------------------------------------------------------

// 依帳本組出 E8-04 `HostEnvironment`：
//   - `host_capabilities` 的每一項 → `provide_capability()`（對接 manifest 的 `requires`；
//     能力由 host 自身決定，不是權限決策的一部分，故由呼叫端傳入）。
//   - 套件宣告的權限中**只有 `Granted`** → `grant_permission()`；
//     `Undecided` 與 `Denied` **都不呼叫**（安全方向；嚴禁樂觀預設，見檔首）。
//
// 只呼叫 E8-04 的公開介面（`provide_capability` / `grant_permission`），E8-04 一行未改。
ds::ext::HostEnvironment compose_host_environment(
    const PermissionLedger& ledger, const Package& pkg,
    const std::vector<std::string>& host_capabilities = {});

// 同上，但直接給權限清單。
ds::ext::HostEnvironment compose_host_environment(
    const PermissionLedger& ledger, const std::string& package_id,
    const std::vector<std::string>& permissions,
    const std::vector<std::string>& host_capabilities = {});

// -----------------------------------------------------------------------------
// 與 E9-10 的接點：安裝時該問哪些權限（Q-C）
// -----------------------------------------------------------------------------

// 一個待詢問的權限請求：哪個套件的哪個權限還沒被問過。
struct PermissionPrompt {
    std::string package_id;
    std::string permission_id;

    bool operator==(const PermissionPrompt& o) const noexcept {
        return package_id == o.package_id && permission_id == o.permission_id;
    }
};

// 掃過 E9-10 持久化的已安裝套件登錄，列出所有**未決**的 (套件, 權限)——
// 也就是 `CHG-20260829-05` 裁定「Q-C 權限在安裝時問」要問的那些。
// 已拒絕的**不**出現在此（問過被拒，不要再問），已授予的亦然。
// 順序：套件字典序（E9-10 `list()` 即字典序），同套件內依 manifest 宣告順序。
// 全程**不載入套件本體**（只讀 E9-10 的持久化中繼資料）。
std::vector<PermissionPrompt> permissions_awaiting_decision(const InstalledPackageStore& installed,
                                                            const PermissionLedger& ledger);

}  // namespace ds::package

#endif  // DS_ENGINE_E9_11_PERMISSION_STORE_HPP
