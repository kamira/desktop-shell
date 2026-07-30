// content/widgets/c2_09/calendar_todo_widget.hpp — C2-09 行事曆 / 待辦 widget
// （artifact 層 / 相位 1：純資料 / 邏輯組裝，無真實 GUI）
//
// 「行事曆 / 待辦」是一個桌面小工具（widget）：把待辦事項依**日期分組**顯示（一份行事曆式清單），
// 每個事項可標記完成 / 未完成。本單元不是新引擎邏輯，而是把四個已合併的擴充點**組裝**成一個
// 具體 widget（lb=0，非其他單元依賴的基礎）：
//
//   - C1-01（`ds::profiles::SkinProfile`）：本 widget 的**桌面殼層基底**——可自由拖曳、具名
//     圖層歸屬、可互動、具透明外形；widget 的圖層 / 定位 / 拖曳皆委派其自身 `shell()`
//     （呼叫端可直接驅動 `shell().load_skin(...)` / `place` / `begin_drag` 等）。
//   - E7-13（`ds::format::Item` / `build_forest`）：以**樹狀結構表達「日期分組 → 事項」**——
//     森林（forest）的每個頂層 `Item` 是一個日期分組（`label` = 日期字串），其 `children` 為
//     該日期下的待辦事項（`label` = 事項文字，`value` = `{done: bool}` 附帶酬載）。
//   - E7-12（`ds::format::serialize` / E7-01 `parse`）：**序列化存載**——`load()` 消費一份
//     E7-01 宣告式文字（根層 `days` 鍵為森林清單），`save()` 反向重建同構的 Value 樹並序列化
//     回文字，round-trip（parse → 改值 → serialize → 再 parse）一致。
//   - E4-01（`ds::render::TextLayout`）：以**文字**呈現目前的行事曆 / 待辦清單（每個日期一行
//     標題，其下每個事項一行 `[x]` / `[ ]` 前綴 + 文字），排版目標 surface 綁定本 widget 的
//     C1-01 具名 `SurfaceId`（`shell().id()`），與殼層一致（NFR-02 具名指涉）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02）。`load()` / `save()` 只操作呼叫端
// 提供的文字字串——不觸碰任何真實檔案 I/O（注入式儲存：讀寫皆由呼叫端負責，本單元只做
// 記憶體內的資料轉換）。任何無效操作（空 id / 重複 id / 找不到事項 / 對日期分組誤用事項
// 操作 / 無法解析的宣告式文字）一律回傳具名結果，不靜默。
//
// 依賴注入約定（皆不擁有其生命週期，須比本物件活得久）：
//   - `ds::kernel::KernelBackend&` / `ds::kernel::LayerStack&`：透傳給內部的 C1-01 `SkinProfile`
//     殼層（同 C1-01 的注入約定）。
//   - `ds::render::FontMetrics&`：`render_model()` 排版所需的字型度量（相位 1 以
//     `ds::render::FixedFontMetrics` 或測試 stub 提供）。
#ifndef DS_CONTENT_WIDGETS_C2_09_CALENDAR_TODO_WIDGET_HPP
#define DS_CONTENT_WIDGETS_C2_09_CALENDAR_TODO_WIDGET_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "document.hpp"      // E7-01（上游，可讀不可改，經 e7_12/e7_13 傳遞）：Value / Document / parse
#include "item_tree.hpp"     // E7-13（上游，可讀不可改）：Item / build_forest / ForestResult
#include "skin_profile.hpp"  // C1-01（上游，可讀不可改）：SkinProfile 桌面殼層基底
#include "text_layout.hpp"   // E4-01（上游，可讀不可改）：FontMetrics / TextLayout / LayoutResult
#include "writeback.hpp"     // E7-12（上游，可讀不可改）：serialize（序列化存載）

namespace ds::widgets {

// 行事曆 / 待辦 widget 操作的具名結果（NFR-02：具名，非布林裸值）。
enum class TodoStatus {
    Ok,            // 操作成功。
    Invalid,       // 前置條件不滿足：空 date / 空 item_id、對日期分組節點誤用事項專屬操作等。
    NotFound,      // 依 id 尋址找不到對應事項。
    DuplicateId,   // add_item 的 item_id 與既有節點（事項或日期分組）id 衝突。
    ParseError,    // load() 的輸入文字無法以 E7-01 解析（不相容 / 語法錯誤）。
};

const char* to_string(TodoStatus s) noexcept;

// ---------------------------------------------------------------------------
// CalendarTodoWidget —— 行事曆 / 待辦 widget：組裝 C1-01 + E7-13 + E7-12 + E4-01。
//
// 每個實例代表**一個**具名桌面 widget（如 "widget.todo"）。內部自持一個以注入後端 / 圖層堆疊
// 建構的 C1-01 `SkinProfile` 殼層，並持有一份 E7-13 森林（`days_`：日期分組 → 事項）。
// ---------------------------------------------------------------------------
class CalendarTodoWidget {
public:
    // 建構一個具名 widget。id 即其殼層 surface 的具名 SurfaceId（NFR-02），亦作為 render_model()
    // 排版目標 surface。後端 / 圖層堆疊 / 字型度量皆為注入式相依（不取得所有權）。
    CalendarTodoWidget(std::string id, ds::kernel::KernelBackend& backend,
                       ds::kernel::LayerStack& layers, const ds::render::FontMetrics& metrics);

    CalendarTodoWidget(const CalendarTodoWidget&) = delete;
    CalendarTodoWidget& operator=(const CalendarTodoWidget&) = delete;

    // --- C1-01 桌面殼層（掛載基底）：圖層 / 定位 / 拖曳皆委派其自身 API。 ---
    ds::profiles::SkinProfile& shell() noexcept { return shell_; }
    const ds::profiles::SkinProfile& shell() const noexcept { return shell_; }

    const std::string& id() const noexcept { return shell_.id(); }

    // --- load：消費 E7-01 宣告式文字（E7-12 之對偶方向），以 E7-13 build_forest 建樹 ---
    //
    // 文字須為合法 E7-01 文件；根層 `days` 鍵（選填，缺省視為空清單）須為 list，每元素為
    // 一個項目 map（日期分組，其 `children` 為事項）。解析 / 相容性失敗 → ParseError；
    // `days` 存在但非 list、或森林建構契約違反（重複 id / 型別錯 / 未知鍵）→ Invalid。
    // 兩者皆不改動既有狀態（不靜默、全有或全無）。成功 → Ok，狀態整份取代（非合併）。
    TodoStatus load(const std::string& text);

    // --- add_item：於指定日期分組新增一筆待辦事項（分組不存在則建立） ---
    //
    // date / item_id 為空 → Invalid。item_id 與現有任何節點（事項或日期分組）id 衝突 →
    // DuplicateId（不改動狀態）。成功 → Ok，新事項預設 done=false，附加於該日期分組尾端
    // （日期分組本身若新建則附加於整體森林尾端，保序）。
    TodoStatus add_item(const std::string& date, const std::string& item_id,
                        const std::string& text);

    // --- toggle_done：翻轉一筆事項的完成狀態 ---
    //
    // item_id 對應日期分組本身（而非事項）→ Invalid（分組無完成狀態語意）。找不到 → NotFound。
    // 成功 → Ok，done 由目前值翻轉（缺 done 鍵視為 false）。
    TodoStatus toggle_done(const std::string& item_id);

    // --- list_items：查詢指定日期分組下的事項清單 ---
    //
    // 日期分組不存在 → 回空清單（非錯誤；以 has_date() 另行查詢是否存在）。
    const std::vector<ds::format::Item>& list_items(const std::string& date) const;

    // --- save：把目前森林序列化回 E7-01 文字（E7-12 serialize），供呼叫端持久化 ---
    //
    // 產出的文字經 load() 再解析可重建等價森林（round-trip；深層 `Item::operator==`）。
    // 本單元不觸碰檔案 I/O——文字的實際存放（檔案 / 設定服務等）由呼叫端負責（注入式儲存）。
    std::string save() const;

    // --- render_model：以 E4-01 文字排版呈現目前的行事曆 / 待辦清單 ---
    //
    // 每個日期分組一行標題（其 label），其下每個事項一行 `[x] ` / `[ ] ` 前綴 + 事項文字。
    // 排版目標 surface 綁定本 widget 的具名 id（與殼層一致）。空森林 → 空字串 → 空排版結果
    // （E4-01 契約：空字串輸入回空結果）。
    ds::render::LayoutResult render_model(
        const ds::render::LayoutConstraints& constraints = ds::render::LayoutConstraints{}) const;

    // --- 查詢 ---
    const std::vector<ds::format::Item>& days() const noexcept { return days_; }
    std::vector<std::string> dates() const;
    bool has_date(const std::string& date) const;
    std::size_t date_count() const noexcept { return days_.size(); }
    std::size_t item_count() const noexcept;
    bool empty() const noexcept { return days_.empty(); }
    bool contains(const std::string& item_id) const;

    // 查詢單一事項的完成狀態。item_id 為日期分組 → Invalid；找不到 → NotFound；
    // 成功 → Ok，*out 填入（缺 done 鍵視為 false）。
    TodoStatus is_done(const std::string& item_id, bool& out) const;

private:
    static std::string date_group_id(const std::string& date);
    static ds::format::Value to_value(const ds::format::Item& item);

    ds::format::Item* find_date_group(const std::string& date);
    const ds::format::Item* find_date_group(const std::string& date) const;
    ds::format::Item* find_any(const std::string& id);
    const ds::format::Item* find_any(const std::string& id) const;
    bool is_date_group_id(const std::string& id) const;

    ds::profiles::SkinProfile shell_;         // C1-01 桌面殼層基底（注入後端 / 圖層堆疊）。
    const ds::render::FontMetrics& metrics_;  // E4-01 render_model() 字型度量（注入）。
    std::vector<ds::format::Item> days_;      // E7-13 森林：日期分組（頂層）→ 事項（子層）。
};

}  // namespace ds::widgets

#endif  // DS_CONTENT_WIDGETS_C2_09_CALENDAR_TODO_WIDGET_HPP
