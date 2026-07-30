// apps/c4_01/shortcut_cheat_sheet.hpp — C4-01 鍵位速查（artifact 層 / apps，相位 1）
//
// 「鍵位速查」（keyboard shortcut cheat sheet）：顯示目前可用快捷鍵清單的速查 HUD 面板
// （鍵位 → 說明對應表）。本單元不是新引擎邏輯，而是把兩個已合併的擴充點**組裝**成單一
// 應用 + 行為（`load_shortcuts(清單)` / `show` / `hide` / `filter`）：
//
//   - C1-04（`ds::profiles::OsdOverlayProfile`）：面板本身的顯隱生命週期——本單元不重造
//     顯隱狀態機，而是**掛**（以參考持有，不取得所有權）一個既有 OSD 浮層 profile 當作
//     速查面板的基底：`show()` = 組出目前可見鍵位的排版文字後委派 `OsdOverlayProfile::show()`
//     （E1-14 短暫生命週期 + E1-01 具名頂層 Overlay 歸屬皆由 C1-04 負責）；`hide()` = 委派
//     `dismiss()`。
//   - E4-01（`ds::render::TextLayout`）：把「鍵位 → 說明」清單排成一份**渲染描述**
//     （`LayoutResult`：每個可見鍵位一行）。本層只組字串與呼叫排版，不做真實字型光柵化
//     （字型度量一律經注入式 `FontMetrics`，相位 1 由呼叫端提供 `FixedFontMetrics` 或測試
//     stub）。
//
// 行為組裝：
//   - `load_shortcuts(entries)`：載入鍵位清單（純資料，覆蓋既有清單）。整批驗證——任一
//     項目鍵位或說明為空字串 → 整批拒絕（`LoadStatus::Invalid`），既有清單不變（不留半份
//     殘留）。空清單合法（`LoadStatus::Ok`，清空既有內容）。若面板目前顯示中，成功載入後
//     即時刷新面板內容（委派 `OsdOverlayProfile::update()`），不需呼叫端另外重新 `show()`。
//   - `filter(query)`：以子字串（ASCII 大小寫不敏感）比對鍵位或說明，決定
//     `visible_shortcuts()`；空字串 = 清除篩選（全部可見）。若面板顯示中同樣即時刷新內容。
//   - `show(ttl)` / `hide()`：委派 C1-04 基底 profile，顯示 / 收起面板；語意（重複顯示回
//     false、ttl==0 委派 E1-14 拒絕等）完全沿用 C1-04，本層不重複判斷。
//   - `layout(constraints)`：純函式——不論面板是否顯示中，皆可對「目前可見鍵位」排版求
//     `LayoutResult`（供驗證排版正確，或供未來相位繪製層消費），與顯示狀態脫鉤。
//
// 相位 1（Mac / null 期）約束：純資料 / 文字排版組裝，不接真實 GUI 繪製、無平台分支
// （無 `#ifdef` / win32 / cocoa）、無真實鍵盤 hook（`ShortcutEntry.keys` 僅為顯示用字串，
// 不做鍵盤事件擷取 / 監聽）。核心 API 無數字 z-order / 絕對座標（NFR-02：面板頂層歸屬由
// 上游 C1-04/E1-01 具名處理；E4-01 排版結果全為相對偏移）。任何無效操作（無效鍵位清單、
// 面板已顯示中再次顯示等）一律明確回傳具名結果，不靜默（NFR-04 精神）。
#ifndef DS_APPS_C4_01_SHORTCUT_CHEAT_SHEET_HPP
#define DS_APPS_C4_01_SHORTCUT_CHEAT_SHEET_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "osd_overlay_profile.hpp"  // C1-04（上游，可讀不可改）：OsdOverlayProfile（面板基底）
                                     //   （透傳 E1-14 TransientProfileManager / ds::events::Tick、
                                     //    E1-01 LayerStack、E1-02 InputStrategy）
#include "text_layout.hpp"          // E4-01（上游，可讀不可改）：TextLayout / FontMetrics /
                                     //   LayoutConstraints / LayoutResult

namespace ds::apps {

// load_shortcuts() 的具名結果（NFR-02：具名，非數字）。
enum class LoadStatus {
    Ok,       // 成功載入（含空清單——清空既有內容）。
    Invalid,  // 清單內存在鍵位或說明為空字串的項目——整批拒絕，既有清單維持不變。
};

const char* to_string(LoadStatus s) noexcept;

// 一筆鍵位 → 說明對應（純資料）。`keys` 僅為顯示用字串（如 "Cmd+Shift+P"），本單元不做
// 任何鍵盤事件擷取 / 監聽 / 解析（相位 1 硬性約束：無真實鍵盤 hook）。
struct ShortcutEntry {
    std::string keys;         // 鍵位顯示字串，如 "Cmd+K"。
    std::string description;  // 該鍵位的說明，如 "開啟命令選擇器"。
};

// 單行顯示文字中，鍵位與說明之間的分隔字串（純 ASCII，避免多位元組分隔符的排版 / 編碼
// 邊界情況）。
inline constexpr const char* kEntrySeparator = "  -  ";

// ---------------------------------------------------------------------------
// ShortcutCheatSheetApp —— 鍵位速查應用：組裝 C1-04（面板顯隱基底）+ E4-01（文字排版）。
//
// 以參考持有上游 C1-04 `OsdOverlayProfile`（不取得所有權，沿用 C4-02/C4-03 組裝相依
// 風格，須比本物件活得久）；`TextLayout`（E4-01）以值持有，內部僅存一個 `FontMetrics`
// 參考（同樣不取得所有權，須比本物件活得久）。
// ---------------------------------------------------------------------------
class ShortcutCheatSheetApp {
public:
    ShortcutCheatSheetApp(ds::profiles::OsdOverlayProfile& panel,
                           const ds::render::FontMetrics& metrics);

    ShortcutCheatSheetApp(const ShortcutCheatSheetApp&) = delete;
    ShortcutCheatSheetApp& operator=(const ShortcutCheatSheetApp&) = delete;

    // --- 行為：load_shortcuts（純資料）---

    // 載入鍵位清單，覆蓋既有清單。任一項目 `keys` 或 `description` 為空字串 → 整批拒絕
    // （`Invalid`），既有清單不變。空清單合法（`Ok`，清空既有內容）。若面板目前顯示中，
    // 成功載入後即時刷新面板內容。
    LoadStatus load_shortcuts(std::vector<ShortcutEntry> entries);

    std::size_t shortcut_count() const noexcept { return entries_.size(); }
    const std::vector<ShortcutEntry>& shortcuts() const noexcept { return entries_; }

    // --- 行為：filter ---

    // 設定篩選字串（ASCII 大小寫不敏感子字串比對，比對鍵位或說明任一命中即可見）。空
    // 字串 = 清除篩選（全部可見）。若面板目前顯示中，即時刷新面板內容。
    void filter(std::string query);

    const std::string& filter_query() const noexcept { return filter_query_; }
    bool filter_active() const noexcept { return !filter_query_.empty(); }

    // 目前篩選後可見的鍵位清單（保持原載入順序）。
    std::vector<ShortcutEntry> visible_shortcuts() const;
    std::size_t visible_count() const { return visible_shortcuts().size(); }

    // --- E4-01 排版 ---

    // 目前可見鍵位組成的顯示文字：每個可見鍵位一行，格式為
    // `"<keys>" + kEntrySeparator + "<description>"`，行與行以 '\n' 分隔。無可見鍵位 →
    // 空字串（E4-01 對空字串排版結果同樣為空：無行、size{0,0}）。
    std::string display_text() const;

    // 對目前可見鍵位排版（委派 E4-01 `TextLayout::layout()`）。純函式，與面板是否顯示中
    // 無關（供驗證排版正確，或供未來相位繪製層消費）。
    ds::render::LayoutResult layout(const ds::render::LayoutConstraints& constraints = {}) const;

    // --- 行為：show / hide（委派 C1-04 基底 profile）---

    // 顯示速查面板：以目前可見鍵位的 `display_text()` 委派 `panel_.show(text, ttl)`。
    // 面板已顯示中 / ttl 被 E1-14 拒絕等一律沿用 C1-04 語意（回 false，不重複判斷）。
    bool show(ds::events::Tick ttl);

    // 收起速查面板：委派 `panel_.dismiss()`。面板未顯示中 → false（no-op，不靜默）。
    bool hide();

    bool is_showing() const;
    // 基底 C1-04 面板的具名 id（構造時由呼叫端注入，透傳查詢用）。
    const std::string& panel_id() const;

private:
    // 面板顯示中時，把目前 `display_text()` 推送給面板（委派 `OsdOverlayProfile::update()`；
    // 未顯示中則為 no-op，安全）。
    void refresh_if_showing();

    ds::profiles::OsdOverlayProfile& panel_;
    ds::render::TextLayout layout_engine_;

    std::vector<ShortcutEntry> entries_;
    std::string filter_query_;
};

}  // namespace ds::apps

#endif  // DS_APPS_C4_01_SHORTCUT_CHEAT_SHEET_HPP
