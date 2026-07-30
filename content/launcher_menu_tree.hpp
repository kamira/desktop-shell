// content/c3_01/launcher_menu_tree.hpp — C3-01 啟動器選單樹
// （artifact 層 / 相位 1：純資料結構 + 注入式啟動後端，無真實 GUI）
//
// 「啟動器選單樹」：階層式選單（E7-13 宣告式定義的樹狀結構）——節點可展開 / 收合、可
// 選取；選中**葉節點**時透過 E3-02（啟動程式 / 開檔 / 網頁搜尋致動器，經其上游 E6-01
// 命令匯流排）觸發對應的啟動動作；選中**非葉節點**則不啟動（呼叫端應改用 expand() /
// collapse()）。本單元不是新引擎邏輯，而是**組裝型 artifact**：
//
//   - C1-05（`ds::profiles::SummonPanelProfile`）：本選單樹**掛載**於其上——重用其
//     選項森林儲存（`set_items` / `items()`，本身即透傳 E7-13 `build_forest` / `Item`），
//     不自行另存一份樹狀資料。本單元只**加上**一層 C1-05 沒有的行為：節點展開 / 收合
//     狀態追蹤，以及「選取葉節點 = 透過 E3-02 啟動」的語意。
//   - E7-13（`ds::format::Item` / `build_forest`，經 C1-05 透傳）：選單的階層式資料模型。
//   - E3-02（`ds::actuators` 具名命令 / `contract_version()`）：葉節點的啟動後端 ——
//     葉節點的 `value()`（E7-01 `Value`）以保留鍵 `command`（命令 id 字串）+ 選填 `args`
//     （字串到字串的具名參數 Map）描述一次啟動意圖，本單元轉譯為 E6-01 `CommandArgs` 後
//     交由**注入式** `ds::command::CommandBus&` 分派（相位 1 = 純資料結構 + 注入後端；
//     呼叫端可注入掛了 E3-02 `LaunchActuator`（`NullLaunchBackend`）的匯流排，或任何測試替身
//     匯流排）。本單元不擁有、不建立匯流排或致動器，只消費已掛好的具名命令。
//     提供 `make_launch_program_value` / `make_open_file_value` / `make_web_search_value`
//     三個便捷工廠，直接对齐 E3-02 三個具名命令 id 與其參數鍵名（program/args、path、
//     query/engine），呼叫端組宣告式樹時不需自行記住保留鍵。
//
// 選單葉節點宣告式 value 契約（E7-13 item value 為 Map 時的保留鍵；其餘 value 型態視為
// 「無啟動動作」的純展示節點——select()/activate() 對其一律回 ActivationFailed）：
//     value:
//       command: launch.program   # 必填；非空字串（命令 id，通常為 E3-02 三個具名命令之一，
//                                  #   但本單元不假設固定集合——任何已掛在注入匯流排上的 id 皆可）
//       args:                     # 選填；Map，每個成員值須為字串，逐一轉為 CommandArgs 具名參數
//         program: Calculator.app
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02）。任何無效操作（展開 / 收合不存在或
// 為葉節點的 id、選取不存在 id、選取非葉節點、葉節點缺乏合法啟動宣告）一律明確回傳具名
// 結果，不靜默。
#ifndef DS_CONTENT_C3_01_LAUNCHER_MENU_TREE_HPP
#define DS_CONTENT_C3_01_LAUNCHER_MENU_TREE_HPP

#include <set>
#include <string>

#include "command_bus.hpp"          // E6-01（經 e3_02 透傳，可讀不可改）：CommandBus / CommandArgs /
                                     //   CommandId / CommandResult
#include "item_tree.hpp"            // E7-13（上游，可讀不可改）：Item / Value（經 e7_01 再透傳）
#include "launch_actuator.hpp"      // E3-02（上游，可讀不可改）：三個具名命令 id / contract_version()
#include "summon_panel_profile.hpp"  // C1-05（上游，可讀不可改）：SummonPanelProfile（本單元掛載之基底）

namespace ds::content {

// 選單葉節點宣告式 value 的保留鍵（見檔首契約）。
namespace menu_keys {
inline constexpr const char* kCommand = "command";
inline constexpr const char* kArgs = "args";
}  // namespace menu_keys

// select() / activate() 的具名結果。
enum class SelectOutcome {
    Activated,        // 葉節點，已透過注入的命令匯流排成功分派（CommandResult.ok() == true）。
    ActivationFailed,  // 葉節點，但啟動失敗——value 非 Map / 缺 'command' / args 型別不符，
                       //   或底層 CommandBus::dispatch 回非 Ok（含未知命令 NotFound）。
    NotLeaf,           // 找到節點，但非葉節點（有子項目）——不啟動；呼叫端應改用
                       //   expand() / collapse()。
    NotFound,          // 整座選單森林中找不到該 id。
};

const char* to_string(SelectOutcome r) noexcept;

// ---------------------------------------------------------------------------
// LauncherMenuTree —— 啟動器選單樹：掛載於 C1-05 基底 profile 上，加上展開 / 收合
// 狀態追蹤 + 「選取葉節點透過 E3-02 啟動」的行為層。
//
// 注入式相依（不擁有其生命週期，須比本物件活得久）：
//   - `ds::profiles::SummonPanelProfile&`（C1-05）：選單資料（項目森林）的宿主——
//     `load_menu()` 委派其 `set_items()`；`items()` 直接讀其 `items()`。
//   - `ds::command::CommandBus&`（E6-01，經 E3-02 透傳）：葉節點啟動時的分派目標。
//     本單元不假設其上已掛哪些命令（相位 1「注入式啟動後端」——呼叫端決定掛什麼，典型為
//     E3-02 `LaunchActuator::register_on(bus)`）。
// ---------------------------------------------------------------------------
class LauncherMenuTree {
public:
    LauncherMenuTree(ds::profiles::SummonPanelProfile& base, ds::command::CommandBus& bus)
        : base_(base), bus_(bus) {}

    LauncherMenuTree(const LauncherMenuTree&) = delete;
    LauncherMenuTree& operator=(const LauncherMenuTree&) = delete;

    // --- load_menu：委派 C1-05 set_items（= E7-13 build_forest / 程式化提供）---

    // 以一個宣告式 Value（須為 List，每元素為 E7-13 項目 Map）取代選單森林。
    // 建構失敗（結構違反）→ 回 false，**現有選單不動**；失敗細節見 last_build_error()。
    // 換上新樹時，舊的展開狀態集合會被清空（避免殘留指向新樹中不存在 id 的展開紀錄）。
    bool load_menu(const ds::format::Value& declarative_list);

    // 程式化提供選單森林（供測試 / 不走宣告式文件的呼叫端）。同樣清空展開狀態。
    void load_menu(std::vector<ds::format::Item> items);

    const std::vector<ds::format::Item>& items() const noexcept { return base_.items(); }

    const ds::format::BuildError& last_build_error() const noexcept {
        return base_.last_build_error();
    }

    // --- 展開 / 收合（純資料狀態追蹤；本單元自有，C1-05 / E7-13 皆無此概念）---

    // 展開一個非葉節點。
    //   - id 不存在於森林中 → false。
    //   - id 為葉節點（無子項目，展開無意義）→ false。
    //   - 已展開中 → false（no-op，不靜默重複展開）。
    bool expand(const std::string& id);

    // 收合一個非葉節點。
    //   - id 不存在、或為葉節點 → false。
    //   - 尚未展開 → false（no-op）。
    bool collapse(const std::string& id);

    // 查詢是否已展開（未曾 expand 過 / 已 collapse / id 不存在 一律回 false）。
    bool is_expanded(const std::string& id) const noexcept;

    // 目前展開中的節點數。
    std::size_t expanded_count() const noexcept { return expanded_.size(); }

    // --- 選取 / 啟動 ---

    // 依 id 於整座森林尋找並選取：
    //   - 找不到 → NotFound。
    //   - 找到但非葉節點 → NotLeaf（不啟動，不影響展開狀態）。
    //   - 找到且為葉節點 → 委派 activate(id)。
    SelectOutcome select(const std::string& id);

    // 直接對一個（預期為葉節點的）id 觸發啟動，跳過「選取」語意，供呼叫端 / 測試直接
    // 驗證 E3-02 組裝是否正確。語意與 select() 對葉節點的分支完全一致（select() 內部即
    // 呼叫本函式）。
    //   - 找不到 → NotFound；非葉節點 → NotLeaf。
    //   - 葉節點 value 非 Map、缺合法 'command'、或 'args' 型別不符 → ActivationFailed
    //     （last_command_result() 帶可讀失敗訊息，狀態為 Failed）。
    //   - 合法 → 交由注入的 CommandBus 分派；CommandResult.ok() 則 Activated，否則
    //     ActivationFailed（含匯流排回報 NotFound，即該命令未被任何致動器掛上）。
    SelectOutcome activate(const std::string& id);

    // 最近一次 activate()（直接呼叫或經 select() 觸發）的命令結果。從未啟動過則為預設值
    // （CommandStatus::Ok，空 value / message —— 呼叫前請先確認曾成功呼叫過 select/activate）。
    const ds::command::CommandResult& last_command_result() const noexcept {
        return last_command_result_;
    }

private:
    const ds::format::Item* find_node(const std::string& id) const;
    bool parse_leaf_action(const ds::format::Item& leaf, ds::command::CommandId& out_id,
                           ds::command::CommandArgs& out_args, std::string& out_error) const;

    ds::profiles::SummonPanelProfile& base_;
    ds::command::CommandBus& bus_;

    std::set<std::string> expanded_;
    ds::command::CommandResult last_command_result_{};
};

// ---------------------------------------------------------------------------
// 便捷工廠 —— 直接对齐 E3-02 三個具名命令 id 與其參數鍵名，組出符合本單元葉節點契約的
// value（見檔首）。呼叫端組宣告式選單樹時可用這些工廠產生 `value:` 子樹，不需自行記住
// 保留鍵名 / E3-02 命令 id 字面值。
// ---------------------------------------------------------------------------

// 對應 E3-02 `launch.program`：program 必填，args 選填（單一命令列參數字串）。
ds::format::Value make_launch_program_value(std::string program, std::string args = std::string());

// 對應 E3-02 `open.file`：path 必填。
ds::format::Value make_open_file_value(std::string path);

// 對應 E3-02 `web.search`：query 必填，engine 選填。
ds::format::Value make_web_search_value(std::string query, std::string engine = std::string());

}  // namespace ds::content

#endif  // DS_CONTENT_C3_01_LAUNCHER_MENU_TREE_HPP
