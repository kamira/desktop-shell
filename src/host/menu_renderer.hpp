// E11-02 自繪選單呈現 — 平台中立介面（platform 層 / 宿主 host / 相位 1 = Mac / null 期）
//
// 語意：以**應用自己的渲染**（owner-drawn）呈現選單 / 內容選單，不使用任何 OS 原生選單 API。
// 三段組裝：
//   1. 從 E7-13 階層式項目結構（`ds::format::Item` 森林）建出選單樹（`build_menu`）——
//      以 `Item::value()`（保留鍵 Map）宣告每個項目的選單語意（型別 / 勾選 / 停用）。
//   2. 以 E4-01 `FontMetrics` / `TextLayout` 為每一列標籤排版，產出相對佈局的渲染描述。
//   3. 以 E1-03 具名 `SurfaceId` 表達每個展開層（面板）的呈現目標，供後續相位的繪製層消費。
//
// 支援：子選單（遞迴展開，一次一條巡覽鏈）、分隔線、勾選項、停用態、鍵盤巡覽
// （move_next/move_prev/enter/exit/activate）與滑鼠巡覽（hover/select）。
//
// **相位 1 硬約束**：本檔只寫介面 + 渲染描述模型，**不**呼叫任何真實選單 API、**不**含
// `#ifdef` / win32 / cocoa / NSMenu 等平台分支，未建立 `src/**/backend/win32|cocoa/`。
//
// **NFR-02**：渲染描述全為相對佈局 —— 面板 / 列以**具名 SurfaceId**、**索引路徑**與
// E4-01 的相對 (x, y) 指涉，不含畫面絕對座標，不含數字 z-order（子選單面板以「擁有者
// 項目的具名 id」組出面板 surface 名稱，非數字深度）。
#ifndef DS_HOST_E11_02_MENU_RENDERER_HPP
#define DS_HOST_E11_02_MENU_RENDERER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "alpha_surface.hpp"  // E1-03（上游，可讀不可改）：ds::kernel::SurfaceId
#include "item_tree.hpp"      // E7-13（上游，可讀不可改）：ds::format::Item
#include "text_layout.hpp"    // E4-01（上游，可讀不可改）：FontMetrics / TextLayout / LayoutResult

namespace ds::host {

// ---------------------------------------------------------------------------
// 選單節點型別 —— 具名（非數字），鏡射 E11-01 TrayMenuItem 的四型別慣例。
// ---------------------------------------------------------------------------
enum class MenuNodeKind { Action, Separator, Checkbox, Submenu };

// 從 E7-13 Item 建選單的宣告式慣例：`Item::value()` 若為 Map，可含下列保留鍵
// （皆選填）；`value()` 為 Null（預設）等同全部省略。非 Null 且非 Map → 建構失敗。
namespace menu_item_keys {
inline constexpr const char* kKind = "kind";        // "action"|"separator"|"checkbox"|"submenu"
inline constexpr const char* kChecked = "checked";  // bool；僅 checkbox 項可用
inline constexpr const char* kEnabled = "enabled";  // bool；預設 true
}  // namespace menu_item_keys

// ---------------------------------------------------------------------------
// MenuNode —— 由 E7-13 Item 轉譯而成的選單樹節點，攜帶選單語意。值語意（可複製）。
// ---------------------------------------------------------------------------
class MenuNode {
public:
    MenuNode() = default;
    MenuNode(std::string id, std::string label, MenuNodeKind kind, bool enabled, bool checked);

    const std::string& id() const noexcept { return id_; }
    const std::string& label() const noexcept { return label_; }
    MenuNodeKind kind() const noexcept { return kind_; }
    bool enabled() const noexcept { return enabled_; }
    bool checked() const noexcept { return checked_; }
    const std::vector<MenuNode>& children() const noexcept { return children_; }
    std::vector<MenuNode>& children() noexcept { return children_; }

    bool is_separator() const noexcept { return kind_ == MenuNodeKind::Separator; }
    bool is_submenu() const noexcept { return kind_ == MenuNodeKind::Submenu; }

    // 可高亮巡覽（鍵盤 / 滑鼠可停駐）：已啟用且非分隔線。
    bool is_navigable() const noexcept { return enabled_ && kind_ != MenuNodeKind::Separator; }
    // 可直接致動（Action / Checkbox 且已啟用）；Submenu 走 enter()，分隔線恆不可。
    bool is_activatable() const noexcept {
        return enabled_ && (kind_ == MenuNodeKind::Action || kind_ == MenuNodeKind::Checkbox);
    }

    MenuNode& add_child(MenuNode child) {
        children_.push_back(std::move(child));
        return *this;
    }
    // 切換勾選狀態（僅 Checkbox 生效）；回傳切換後的新狀態。
    bool toggle_checked() noexcept {
        if (kind_ == MenuNodeKind::Checkbox) checked_ = !checked_;
        return checked_;
    }
    void set_checked(bool c) noexcept { checked_ = c; }

private:
    std::string id_;
    std::string label_;
    MenuNodeKind kind_ = MenuNodeKind::Action;
    bool enabled_ = true;
    bool checked_ = false;
    std::vector<MenuNode> children_;
};

// 建構錯誤 —— 承 E7-13 慣例：可定位訊息，不得靜默失敗。
struct MenuBuildError {
    std::string message;
};

// ---------------------------------------------------------------------------
// MenuModel —— 選單樹模型：頂層有序節點集合（項目本身可遞迴含子選單）。
// ---------------------------------------------------------------------------
class MenuModel {
public:
    MenuModel() = default;
    explicit MenuModel(std::vector<MenuNode> items) : items_(std::move(items)) {}

    const std::vector<MenuNode>& items() const noexcept { return items_; }
    std::size_t size() const noexcept { return items_.size(); }
    bool empty() const noexcept { return items_.empty(); }

    // 依索引路徑定位（同 E11-01 TrayMenu::at_path 慣例）：path[0] 為頂層索引，
    // 其後每層進入前一項的 children。越界或穿越非子選單項 → nullptr（不崩潰）。
    MenuNode* at_path(const std::vector<std::size_t>& path);
    const MenuNode* at_path(const std::vector<std::size_t>& path) const;

private:
    std::vector<MenuNode> items_;
};

// 建構結果：成功持有 MenuModel，失敗持有 MenuBuildError。二者互斥。
class MenuBuildResult {
public:
    static MenuBuildResult success(MenuModel model);
    static MenuBuildResult failure(MenuBuildError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    const MenuModel& model() const { return model_; }        // 僅 ok() 為 true 時有效。
    const MenuBuildError& error() const { return error_; }    // 僅 ok() 為 false 時有效。

private:
    MenuBuildResult() = default;
    bool ok_ = false;
    MenuModel model_;
    MenuBuildError error_;
};

// 從 E7-13 森林（頂層 Item 有序集合）建選單樹。見 `menu_item_keys` 宣告式慣例：
//   - `kind` 省略：有子項 → Submenu，否則 → Action。
//   - 只有 Submenu 可帶子項；Action/Separator/Checkbox 帶子項 → 失敗。
//   - `checked` 只在 kind == checkbox 時允許；`kind` 值非四者之一、`checked` /
//     `enabled` 型別不符、`value()` 非 Null 亦非 Map → 失敗（BuildError 可定位訊息）。
// 空森林 → 成功、空模型。
MenuBuildResult build_menu(const std::vector<ds::format::Item>& forest);
// 便利重載：以單一根項目的 `children()` 作為頂層選單項森林。
MenuBuildResult build_menu(const ds::format::Item& root);

// ---------------------------------------------------------------------------
// 選單渲染描述 —— NFR-02：全相對佈局，具名 surface 指涉，無絕對座標 / 數字 z-order。
// ---------------------------------------------------------------------------

// 一列選單項的渲染描述。
struct MenuRowRender {
    std::vector<std::size_t> path;      // 於整棵選單模型的索引路徑
    MenuNodeKind kind = MenuNodeKind::Action;
    ds::render::LayoutResult label;      // E4-01 排版描述（分隔線為預設空排版）
    bool enabled = true;
    bool checked = false;
    bool has_submenu = false;
    bool selected = false;               // 是否為目前巡覽游標所在列
    double y = 0.0;                      // 面板內相對 y 偏移（累積列高）
    double row_height = 0.0;             // 本列高度
};

// 一個選單面板（根選單，或某層已展開的子選單）的渲染描述。
struct MenuPanelRender {
    ds::kernel::SurfaceId surface;              // 具名呈現目標（NFR-02）
    std::vector<std::size_t> owner_path;        // 開啟此面板的子選單項路徑（根面板為空）
    std::vector<MenuRowRender> rows;
    ds::render::Size size;                       // 寬 = 最寬列標籤、高 = 各列高度總和
};

// 完整選單渲染描述：由根面板起，依目前展開巡覽鏈依序排列的面板清單。
struct MenuRenderModel {
    std::vector<MenuPanelRender> panels;
};

// 巡覽 / 選取結果狀態 —— 結構化，絕不崩潰。
enum class MenuNavStatus {
    Moved,          // 巡覽游標已於目前層內移動 / 定位（hover）
    Entered,        // 進入子選單（展開新面板，游標移入其第一可巡覽列）
    Exited,         // 退出（收合最深面板，回上一層）
    Selected,       // Action / Checkbox 已致動
    NotSelectable,  // 命中分隔線 / 停用項 / 空子選單（無可進入項）
    Invalid,        // 路徑不存在、游標為空、或操作於目前狀態不成立
    Empty,          // 選單模型無任何項目
};

struct MenuNavResult {
    MenuNavStatus status = MenuNavStatus::Invalid;
    std::vector<std::size_t> path;    // 結果所指路徑（游標目前所在；失敗時可能為空）
    bool checked = false;             // Checkbox 致動後的新勾選狀態（其餘情形恆為 false）

    // 是否為「已改變狀態」的成功結果（巡覽移動 / 進入 / 退出 / 選取）。
    bool ok() const noexcept {
        return status == MenuNavStatus::Moved || status == MenuNavStatus::Entered ||
               status == MenuNavStatus::Exited || status == MenuNavStatus::Selected;
    }
};

// ---------------------------------------------------------------------------
// CustomMenuRenderer —— 自繪選單呈現：模型 + 排版 + 具名面板渲染描述 + 巡覽/選取。
//
// 以注入的 E4-01 `FontMetrics` 為每列標籤排版；每個展開層對應一個具名 E1-03 `SurfaceId`
// 面板（根面板 = 建構時給定的 root surface；子層面板名以其擁有者子選單項的具名 id 組成，
// 不以數字深度 / 索引指涉）。呼叫端消費 `render_model()` 取得目前可見面板 / 列的完整
// 相對幾何描述，自行繪製（owner-drawn），全程不經任何 OS 選單 API。
// ---------------------------------------------------------------------------
class CustomMenuRenderer {
public:
    // 綁定字型度量（不取得所有權；須存活於本物件之外的生命週期內）與根面板具名 surface。
    CustomMenuRenderer(const ds::render::FontMetrics& metrics, ds::kernel::SurfaceId root_surface);

    // --- 模型 ---
    // 由 E7-13 森林建選單並取代目前模型；成功時巡覽游標重置（清空）。
    // 建構失敗時目前模型與游標維持不變（呼叫端可依 BuildError 決定重試 / 中止）。
    MenuBuildResult set_menu(const std::vector<ds::format::Item>& forest);
    const MenuModel& model() const noexcept { return model_; }
    bool has_menu() const noexcept { return !model_.empty(); }

    // --- 標籤排版約束（套用於每列標籤；預設值見 E4-01 LayoutConstraints） ---
    void set_label_constraints(ds::render::LayoutConstraints c) { constraints_ = std::move(c); }
    const ds::render::LayoutConstraints& label_constraints() const noexcept { return constraints_; }

    // --- 巡覽（鍵盤） ---
    // 目前層內移至下一 / 上一個可巡覽列（跳過分隔線 / 停用項；循環）。
    // 尚無游標時，move_next 停駐第一個可巡覽列，move_prev 停駐最後一個。
    MenuNavResult move_next();
    MenuNavResult move_prev();
    // 目前高亮列須為已啟用的 Submenu 且至少一個可巡覽子項，否則 NotSelectable。
    // 成功：展開一層新面板，游標移入其第一可巡覽列。
    MenuNavResult enter();
    // 收合最深面板，回上一層（游標退回其擁有者列）。游標深度 <= 1（已在根層）→ Invalid。
    MenuNavResult exit();
    // 致動目前高亮列：Action/Checkbox → Selected（Checkbox 同時切換勾選）；
    // Submenu → 等同 enter()；分隔線 / 停用項 → NotSelectable；尚無游標 → Invalid。
    MenuNavResult activate();

    // --- 巡覽（滑鼠） ---
    // 將游標直接定位到指定路徑（滑鼠移入列時呼叫）。path 必須指向目前展開鏈內
    // 「可見面板」中的一列（即該路徑的上層祖先與目前游標一致），且該節點須可巡覽；
    // 否則不變更游標並回 Invalid / NotSelectable。
    MenuNavResult hover(const std::vector<std::size_t>& path);
    // 直接選取指定路徑（滑鼠點擊）：等同先 hover 成功、再 activate()。
    // hover 失敗時直接回傳該失敗結果（游標不變）。
    MenuNavResult select(const std::vector<std::size_t>& path);

    // --- 狀態查詢 ---
    const std::vector<std::size_t>& current() const noexcept { return cursor_; }

    // --- 渲染描述 ---
    // 依目前展開鏈 + 游標，排版每列標籤並產出完整渲染描述（NFR-02：全相對佈局）。
    // 選單為空 → 回傳空面板清單。
    MenuRenderModel render_model() const;

private:
    const std::vector<MenuNode>* level_at(const std::vector<std::size_t>& parent_path) const;
    bool path_visible(const std::vector<std::size_t>& path) const;
    MenuNavResult move_by(bool forward);
    ds::kernel::SurfaceId panel_surface_name(const std::vector<std::size_t>& parent_path) const;

    const ds::render::FontMetrics& metrics_;
    ds::render::TextLayout text_layout_;  // TextLayout::layout() 本身為 const；surface 逐列覆寫
    ds::kernel::SurfaceId root_surface_;
    MenuModel model_;
    ds::render::LayoutConstraints constraints_;
    std::vector<std::size_t> cursor_;  // 目前高亮列路徑；空 = 尚無巡覽游標
};

}  // namespace ds::host

#endif  // DS_HOST_E11_02_MENU_RENDERER_HPP
