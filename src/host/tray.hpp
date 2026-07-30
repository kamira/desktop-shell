// E11-01 系統匣圖示與右鍵選單 — 平台中立介面 + null 後端
//
// 「系統匣 / 選單列（system tray / menu bar）圖示與其右鍵選單」的設定介面：
// 設定匣圖示與提示文字、以項目樹建立右鍵選單（標籤 / 分隔線 / 子選單 / 勾選狀態）、
// 選單項點擊經 E6-01 命令匯流排（ds::command::CommandBus）分派、更新圖示 / 選單。
//
// 相位 1（Mac / null 期）約束（platform 層尤其嚴格）：
//   - 只有平台中立介面 + null 後端；不綁任何真實平台後端。
//   - **絕對禁止** `#ifdef _WIN32` / win32 / cocoa / NSStatusBar / Shell_NotifyIcon 等
//     平台分支或真實匣 API（backend_guard 會擋），亦不建立 src/**/backend/win32|cocoa/。
//   - 對系統的操作一律經**可注入的 TrayBackend 抽象**（set_icon / set_tooltip /
//     set_menu / show / hide）；相位 1 由 NullTrayBackend 承接（記憶體狀態、記錄呼叫）。
//   - 選單邏輯（項目樹、點擊→E6-01 分派、勾選切換）與後端**解耦**，不依賴任何真實匣。
#ifndef DS_HOST_E11_01_TRAY_HPP
#define DS_HOST_E11_01_TRAY_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "command_bus.hpp"  // E6-01：ds::command::CommandBus / CommandArgs / CommandResult

namespace ds::host {

// ---------------------------------------------------------------------------
// TrayMenuItem — 右鍵選單的一個項目（可遞迴組成子選單樹）。
//
// 四種型別：
//   - Action    ：一般命令項（有標籤 + 命令 id）；點擊經匯流排分派。
//   - Separator ：分隔線（無標籤、無命令、不可點擊）。
//   - Checkbox  ：勾選項（有勾選狀態）；點擊先切換勾選、再分派命令（帶 checked 參數）。
//   - Submenu   ：子選單（有標籤 + 子項目）；本身不可點擊，容器角色。
// ---------------------------------------------------------------------------
enum class TrayItemKind { Action, Separator, Checkbox, Submenu };

class TrayMenuItem {
public:
    // --- 工廠（意圖清楚地建構各型別項目）---

    // 一般命令項：標籤 + 命令 id；點擊時分派該命令。
    static TrayMenuItem action(std::string label, ds::command::CommandId command_id,
                               bool enabled = true) {
        TrayMenuItem it;
        it.kind_ = TrayItemKind::Action;
        it.label_ = std::move(label);
        it.command_id_ = std::move(command_id);
        it.enabled_ = enabled;
        return it;
    }

    // 分隔線：無標籤、無命令、永不可點擊。
    static TrayMenuItem separator() {
        TrayMenuItem it;
        it.kind_ = TrayItemKind::Separator;
        it.enabled_ = false;
        return it;
    }

    // 勾選項：標籤 + 命令 id + 初始勾選狀態；點擊先切換勾選、再分派。
    static TrayMenuItem checkbox(std::string label, ds::command::CommandId command_id,
                                 bool checked = false, bool enabled = true) {
        TrayMenuItem it;
        it.kind_ = TrayItemKind::Checkbox;
        it.label_ = std::move(label);
        it.command_id_ = std::move(command_id);
        it.checked_ = checked;
        it.enabled_ = enabled;
        return it;
    }

    // 子選單：標籤 + 子項目集合；本身為容器，不可點擊。
    static TrayMenuItem submenu(std::string label, std::vector<TrayMenuItem> children,
                                bool enabled = true) {
        TrayMenuItem it;
        it.kind_ = TrayItemKind::Submenu;
        it.label_ = std::move(label);
        it.children_ = std::move(children);
        it.enabled_ = enabled;
        return it;
    }

    // --- 查詢 ---
    TrayItemKind kind() const noexcept { return kind_; }
    const std::string& label() const noexcept { return label_; }
    const ds::command::CommandId& command_id() const noexcept { return command_id_; }
    bool enabled() const noexcept { return enabled_; }
    bool checked() const noexcept { return checked_; }
    bool has_command() const noexcept { return !command_id_.empty(); }

    // 是否可點擊（分派）：僅 Action / Checkbox 且已啟用。分隔線 / 子選單永不可點擊。
    bool is_clickable() const noexcept {
        return enabled_ && (kind_ == TrayItemKind::Action || kind_ == TrayItemKind::Checkbox);
    }

    // 子項目（子選單用）。
    const std::vector<TrayMenuItem>& children() const noexcept { return children_; }
    std::vector<TrayMenuItem>& children() noexcept { return children_; }

    // --- 變更 ---
    void set_enabled(bool e) noexcept { enabled_ = e; }
    void set_checked(bool c) noexcept { checked_ = c; }
    void set_label(std::string l) { label_ = std::move(l); }

    // 切換勾選狀態；回傳切換後的新狀態。非 Checkbox 型別不變更、回原狀態。
    bool toggle_checked() noexcept {
        if (kind_ == TrayItemKind::Checkbox) checked_ = !checked_;
        return checked_;
    }

private:
    TrayMenuItem() = default;

    TrayItemKind kind_ = TrayItemKind::Action;
    std::string label_;
    ds::command::CommandId command_id_;
    bool enabled_ = true;
    bool checked_ = false;
    std::vector<TrayMenuItem> children_;
};

// ---------------------------------------------------------------------------
// TrayMenu — 右鍵選單模型（項目的有序集合，項目本身可遞迴含子選單）。
//
// 純資料 / 邏輯，與任何真實匣後端解耦。以索引路徑（index path）定位巢狀項目，
// 遍歷 / 測試皆具決定性。
// ---------------------------------------------------------------------------
class TrayMenu {
public:
    TrayMenu() = default;

    // 追加一個已建好的項目；回傳 *this 以便鏈式建構。
    TrayMenu& add(TrayMenuItem item) {
        items_.push_back(std::move(item));
        return *this;
    }

    // 便捷建構子（鏈式）。
    TrayMenu& add_action(std::string label, ds::command::CommandId command_id,
                         bool enabled = true) {
        return add(TrayMenuItem::action(std::move(label), std::move(command_id), enabled));
    }
    TrayMenu& add_separator() { return add(TrayMenuItem::separator()); }
    TrayMenu& add_checkbox(std::string label, ds::command::CommandId command_id,
                           bool checked = false, bool enabled = true) {
        return add(TrayMenuItem::checkbox(std::move(label), std::move(command_id), checked,
                                          enabled));
    }
    TrayMenu& add_submenu(std::string label, std::vector<TrayMenuItem> children,
                          bool enabled = true) {
        return add(TrayMenuItem::submenu(std::move(label), std::move(children), enabled));
    }

    const std::vector<TrayMenuItem>& items() const noexcept { return items_; }
    std::vector<TrayMenuItem>& items() noexcept { return items_; }
    std::size_t size() const noexcept { return items_.size(); }
    bool empty() const noexcept { return items_.empty(); }

    // 依索引路徑定位項目（支援巢狀子選單）：path[0] 為頂層索引，其後每一層進入
    // 前一項的 children。任一索引越界或路徑穿越非子選單項則回 nullptr（不崩潰）。
    TrayMenuItem* at_path(const std::vector<std::size_t>& path) {
        if (path.empty()) return nullptr;
        std::vector<TrayMenuItem>* level = &items_;
        TrayMenuItem* current = nullptr;
        for (std::size_t depth = 0; depth < path.size(); ++depth) {
            std::size_t idx = path[depth];
            if (level == nullptr || idx >= level->size()) return nullptr;
            current = &(*level)[idx];
            level = &current->children();  // 下一層（非子選單時為空，越界即回 nullptr）
        }
        return current;
    }
    const TrayMenuItem* at_path(const std::vector<std::size_t>& path) const {
        return const_cast<TrayMenu*>(this)->at_path(path);
    }

private:
    std::vector<TrayMenuItem> items_;
};

// ---------------------------------------------------------------------------
// TrayBackend — 可注入的匣後端抽象（platform 層對系統的唯一出口）。
//
// 高階選單邏輯只透過此介面對「系統」下達操作，因此與真實平台完全解耦。相位 1 由
// NullTrayBackend 承接；真實後端（相位 2+）實作同介面即可，呼叫端與選單邏輯不變。
// ---------------------------------------------------------------------------
class TrayBackend {
public:
    virtual ~TrayBackend() = default;

    // 本後端是否對應一個真實系統匣。相位 1 null 後端回 false（能力閘控入口，NFR-03 精神）。
    virtual bool has() const = 0;

    // 設定匣圖示（以穩定的具名識別字承載，不含任何平台 handle / 像素）。
    virtual void set_icon(const std::string& icon) = 0;

    // 設定滑鼠停留提示文字。
    virtual void set_tooltip(const std::string& tooltip) = 0;

    // 推送目前選單模型至後端（每次選單變更後呼叫）。
    virtual void set_menu(const TrayMenu& menu) = 0;

    // 顯示 / 隱藏匣圖示。
    virtual void show() = 0;
    virtual void hide() = 0;
};

// ---------------------------------------------------------------------------
// NullTrayBackend — 相位 1（Mac / null 期）後端。
//
// 不觸碰任何真實系統匣（無 NSStatusBar / Shell_NotifyIcon）；改為在**記憶體**中忠實
// 記錄最後一次的圖示 / 提示 / 選單 / 可見狀態，並累計各操作呼叫次數，供契約測試斷言
// 「狀態一致」。has() 回 false —— 相位 1 無真實匣。
// ---------------------------------------------------------------------------
class NullTrayBackend final : public TrayBackend {
public:
    bool has() const override { return false; }

    void set_icon(const std::string& icon) override {
        icon_ = icon;
        ++set_icon_calls_;
    }
    void set_tooltip(const std::string& tooltip) override {
        tooltip_ = tooltip;
        ++set_tooltip_calls_;
    }
    void set_menu(const TrayMenu& menu) override {
        menu_ = menu;  // 記錄快照
        ++set_menu_calls_;
    }
    void show() override {
        visible_ = true;
        ++show_calls_;
    }
    void hide() override {
        visible_ = false;
        ++hide_calls_;
    }

    // --- 記錄狀態（供測試斷言）---
    const std::string& icon() const noexcept { return icon_; }
    const std::string& tooltip() const noexcept { return tooltip_; }
    const TrayMenu& menu() const noexcept { return menu_; }
    bool visible() const noexcept { return visible_; }

    std::size_t set_icon_calls() const noexcept { return set_icon_calls_; }
    std::size_t set_tooltip_calls() const noexcept { return set_tooltip_calls_; }
    std::size_t set_menu_calls() const noexcept { return set_menu_calls_; }
    std::size_t show_calls() const noexcept { return show_calls_; }
    std::size_t hide_calls() const noexcept { return hide_calls_; }

private:
    std::string icon_;
    std::string tooltip_;
    TrayMenu menu_;
    bool visible_ = false;
    std::size_t set_icon_calls_ = 0;
    std::size_t set_tooltip_calls_ = 0;
    std::size_t set_menu_calls_ = 0;
    std::size_t show_calls_ = 0;
    std::size_t hide_calls_ = 0;
};

// ---------------------------------------------------------------------------
// TrayClickStatus / TrayClickResult — 選單項點擊的結果。
//
// 點擊的邊界情形（路徑無效 / 項目不可點擊 / 無命令 / 未接匯流排）皆結構化回報，
// 絕不崩潰。成功分派時附帶匯流排回傳的 CommandResult。
// ---------------------------------------------------------------------------
enum class TrayClickStatus {
    Dispatched,    // 命令已經匯流排分派（result 為匯流排回傳）
    InvalidPath,   // 索引路徑不存在
    NotClickable,  // 命中分隔線 / 子選單 / 已停用項（不分派）
    NoCommand,     // 可點擊項但未綁命令 id（如純切換的勾選項）
    NoBus,         // 未注入命令匯流排（無法分派）
};

struct TrayClickResult {
    TrayClickStatus status = TrayClickStatus::InvalidPath;
    ds::command::CommandResult command_result{};  // status==Dispatched 時有效
    bool checked = false;  // Checkbox 點擊後的新勾選狀態（其餘情形為 false）

    // 是否既成功分派、且處理器回報成功。
    bool ok() const noexcept {
        return status == TrayClickStatus::Dispatched && command_result.ok();
    }
};

// ---------------------------------------------------------------------------
// SystemTray — 系統匣控制器：整合選單模型 + 可注入後端 + E6-01 命令匯流排。
//
// 職責：
//   - 保存並更新匣圖示 / 提示文字 / 選單模型，並將變更推送至後端。
//   - 依索引路徑點擊選單項：Checkbox 先切換勾選再分派（帶 checked 參數）、Action 直接
//     分派；分派經注入的 CommandBus（呼叫端不相依任何具體致動器）。
//   - 所有對「系統」的操作皆經 TrayBackend，本身無任何平台分支。
//
// 匯流排以 const 指標注入（可為 nullptr —— 此時可點擊項回 NoBus，不崩潰）。
// ---------------------------------------------------------------------------
class SystemTray {
public:
    SystemTray(std::unique_ptr<TrayBackend> backend, const ds::command::CommandBus* bus)
        : backend_(std::move(backend)), bus_(bus) {}

    // 後端是否對應真實系統匣（相位 1 null 後端回 false）。能力閘控入口。
    bool has() const { return backend_ && backend_->has(); }

    // 設定 / 更新匣圖示（具名識別字）；同步推送後端。
    void set_icon(std::string icon) {
        icon_ = std::move(icon);
        if (backend_) backend_->set_icon(icon_);
    }

    // 設定 / 更新提示文字；同步推送後端。
    void set_tooltip(std::string tooltip) {
        tooltip_ = std::move(tooltip);
        if (backend_) backend_->set_tooltip(tooltip_);
    }

    // 以整個選單模型取代目前選單；同步推送後端。
    void set_menu(TrayMenu menu) {
        menu_ = std::move(menu);
        push_menu();
    }

    // 選單模型存取（就地建構 / 變更後可呼叫 sync_menu() 推送後端）。
    TrayMenu& menu() noexcept { return menu_; }
    const TrayMenu& menu() const noexcept { return menu_; }

    // 將目前選單模型重新推送後端（就地修改選單後呼叫）。
    void sync_menu() { push_menu(); }

    // 顯示 / 隱藏匣圖示。
    void show() {
        visible_ = true;
        if (backend_) backend_->show();
    }
    void hide() {
        visible_ = false;
        if (backend_) backend_->hide();
    }
    bool visible() const noexcept { return visible_; }

    const std::string& icon() const noexcept { return icon_; }
    const std::string& tooltip() const noexcept { return tooltip_; }

    // 依索引路徑點擊選單項：
    //   - 路徑無效 → InvalidPath。
    //   - 分隔線 / 子選單 / 停用項 → NotClickable（不分派）。
    //   - Checkbox：先切換勾選狀態並推送後端（backend 反映新狀態），再以帶 "checked"
    //     參數的命令分派。命令 id 為空則回 NoCommand（切換仍生效）。
    //   - Action：直接分派其命令。命令 id 為空則回 NoCommand。
    //   - 未注入匯流排 → NoBus（不崩潰）。
    TrayClickResult click(const std::vector<std::size_t>& path);

    // 直接存取後端（供測試 / 進階整合）。
    TrayBackend* backend() const noexcept { return backend_.get(); }

private:
    void push_menu() {
        if (backend_) backend_->set_menu(menu_);
    }

    std::unique_ptr<TrayBackend> backend_;
    const ds::command::CommandBus* bus_ = nullptr;
    TrayMenu menu_;
    std::string icon_;
    std::string tooltip_;
    bool visible_ = false;
};

// 取得目前平台的預設系統匣控制器。
//
// 相位 1（Mac / null 期）一律以 NullTrayBackend 承接。真實後端上線後由此工廠改派，
// 呼叫端與選單邏輯一行不改（本就經 TrayBackend 抽象操作）。
std::unique_ptr<SystemTray> make_default_tray(const ds::command::CommandBus* bus);

}  // namespace ds::host

#endif  // DS_HOST_E11_01_TRAY_HPP
