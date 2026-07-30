// content/c3_02/dialogue_book.hpp — C3-02 角色對話本（角色劇本 / 對話腳本）
// （artifact 層 / 相位 1：純資料 / 邏輯組裝，無真實 GUI）
//
// 「角色對話本」是一份**用 E8-02 對話腳本直譯器撰寫的台詞序列**，逐句驅動 C1-03 `BalloonProfile`
// 在依附角色旁顯示對話——本單元不是新引擎邏輯，而是把三個已合併的擴充點**組裝**成單一「對話本」
// 應用 profile + 行為：
//
//   - E8-02（`ds::script::Interpreter` / `Script` / `Step`）：**台詞 / 分支**——一段對話本以
//     `Step::say(text)` 表達逐句台詞，`Step::if_goto` / `Step::goto_label` / `Step::label`
//     表達分支與跳轉，`Step::set` 可維護對話期間的變數（供條件分支判斷）。變數狀態在整段對話本
//     播放期間延續（存於本單元自持的 `ExecutionContext`）。
//   - E8-03（`ds::script::PresentationController` / `PresentationSink`）：**舞台指示**——一句
//     `say` 文字若符合表現指令語法（`show` / `hide` / `switch_surface` / `transition` /
//     `wait`，見 `presentation_commands.hpp` 檔首），視為舞台指示而非台詞：經注入的 E6-01
//     `CommandBus` 分派（`switch_surface` 委派 E4-06 `SurfaceSwitcher` 實際切換），**不**顯示
//     於氣球、也不算作一句「台詞」——`advance()` 會跳過它繼續尋找下一句真正的台詞。
//   - C1-03（`ds::profiles::BalloonProfile`）：**逐句顯示**——每次前進到下一句台詞，經
//     `show_balloon(speaker, line, ttl)` 顯示於依附角色（`speaker`）旁；`tick(dt)` 透傳氣球的
//     逐字顯示 / 存活倒數推進。
//
// 對話本以**單一發話角色**（`speaker`，一個已載入的 C1-02 `PortraitProfile`）為對象——多角色
// 對話（切換發話者）非本單元範圍（可用舞台指示 `show <name>` 搭配呼叫端自行協調多顆氣球）。
//
// 播放模型：`load_script(definition)` 載入一份腳本定義（可重複載入，會捨棄既有播放進度）→
// `start(speaker, ttl)` 從腳本起點推進到第一句台詞並顯示 → `advance(speaker, ttl)` 收掉目前
// 氣球、從目前執行位置續跑腳本直到下一句台詞（跳過舞台指示 / 純變數賦值步驟）或腳本結束 →
// `is_finished()` 腳本已無更多台詞 → `reset()` 收掉氣球、捨棄執行進度（腳本定義本身保留，可
// 重新 `start()`）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 `#ifdef` /
// win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02）。空腳本、未 load_script 即
// start/advance、未載入角色、腳本執行期錯誤（未知指令 / 求值失敗 / 未知標籤等）、重複
// start（未 reset）、已結束後再 advance 等一律明確回傳具名結果，不靜默。
#ifndef DS_CONTENT_C3_02_DIALOGUE_BOOK_HPP
#define DS_CONTENT_C3_02_DIALOGUE_BOOK_HPP

#include <memory>
#include <string>

#include "balloon_profile.hpp"        // C1-03（上游，可讀不可改）：BalloonProfile / PortraitProfile
                                       //   （前置宣告，經其 character_bridge.hpp 傳遞）
#include "interpreter.hpp"            // E8-02（上游，可讀不可改）：Script / Step / Interpreter /
                                       //   ScriptError / ExecutionContext
#include "presentation_commands.hpp"  // E8-03（上游，可讀不可改）：PresentationController /
                                       //   PresentationSink；經其標頭傳遞 E6-01 CommandBus /
                                       //   E4-06 SurfaceSwitcher

namespace ds::content {

// 對話本操作的具名結果（NFR-02：具名，非數字）。
enum class DialogueStatus {
    Ok,             // 操作成功，`current_line()` 為新顯示的一句台詞。
    Invalid,        // 前置條件不滿足：未 load_script、空腳本、未 start 即 advance、speaker 未
                     // 載入、重複 start（未先 reset）、已結束後再 advance 等。
    ScriptError,    // E8-02 腳本執行期錯誤（未知指令 / 求值失敗 / 未知標籤等）；見 last_error()。
    Finished,       // 腳本自此已無更多台詞（自然跑到結尾）；氣球已收掉，current_line() 清空。
};

const char* to_string(DialogueStatus s) noexcept;

// ---------------------------------------------------------------------------
// DialogueBook —— 角色對話本：組裝 E8-02（台詞 / 分支）+ E8-03（舞台指示）+ C1-03（逐句顯示）。
// ---------------------------------------------------------------------------
class DialogueBook {
public:
    // id：對話本自身具名 id（轉交內部氣球作為其 surface id）。
    // metrics：字型度量（承 E4-01 慣例，不取得所有權；生命週期須涵蓋本物件）。
    // bus / surfaces：E8-03 舞台指示所需的注入式 CommandBus / SurfaceSwitcher（不取得所有權；
    //   生命週期須涵蓋本物件）。建構時嘗試把五個舞台指示註冊到 bus；若因 id 衝突（如同一 bus
    //   上已有另一個 controller）而失敗，`ready()` 回 false（見該查詢說明），不靜默。
    DialogueBook(std::string id, const ds::render::FontMetrics& metrics, ds::command::CommandBus& bus,
                 ds::render::SurfaceSwitcher& surfaces,
                 ds::render::LayoutConstraints constraints = {});

    // 解構：Interpreter / LineSink（內部 pimpl）需完整定義才可銷毀，故於 .cpp 定義。
    ~DialogueBook();

    DialogueBook(const DialogueBook&) = delete;
    DialogueBook& operator=(const DialogueBook&) = delete;

    // 舞台指示是否成功掛上注入的 CommandBus（透傳 E8-03 `PresentationController::ready()`）。
    // 為 false 時 load_script / start / advance 一律回 Invalid（不靜默用半殘的表現層）。
    bool ready() const noexcept;
    const std::string& registration_error() const noexcept;

    // --- 載入 / 播放 ---

    // 載入一份腳本定義（台詞 + 可能的 set / if / goto / label）。空腳本 → Invalid（不建立可跑
    // 狀態）。成功會捨棄既有播放進度（若曾 start 過，須重新 start()）。
    DialogueStatus load_script(const ds::script::Script& script);

    // 開始播放：以 speaker 為依附角色，從腳本起點推進到第一句台詞並顯示於 C1-03 氣球。
    //   - 未 load_script（或最近一次為空腳本）→ Invalid。
    //   - !ready() → Invalid。
    //   - 已 start 且尚未 Finished / reset() → Invalid（不靜默重播；先 reset() 再重新開始）。
    //   - 腳本執行期錯誤 → ScriptError（見 last_error()）。
    //   - 腳本裡沒有任何台詞（全是舞台指示 / 變數賦值 / 空腳本以外的空轉）→ Finished。
    //   - 找到第一句台詞但 speaker 未載入 / 顯示失敗 → Invalid（腳本執行位置仍前進，須 reset()
    //     後重試；理由見 advance() 說明）。
    DialogueStatus start(const ds::profiles::PortraitProfile& speaker, ds::events::Tick ttl);

    // 推進到下一句：先收掉目前顯示中的氣球（若有），再從目前執行位置續跑腳本直到下一句台詞
    // （跳過舞台指示 / 變數賦值）或腳本結束，新台詞經 C1-03 `show_balloon` 顯示。
    //   - 尚未 start（或已 Finished / reset 後未重新 start）→ Invalid。
    //   - 腳本執行期錯誤 → ScriptError（狀態保留在錯誤前的執行位置，last_error() 可查）。
    //   - 跑到腳本結尾都沒有下一句台詞 → Finished（收掉氣球、current_line() 清空）。
    //   - 找到下一句台詞但顯示失敗（如 speaker 未載入）→ Invalid（腳本執行位置已前進，
    //     current_line() 不更新；此為腳本推進與氣球顯示分屬兩個獨立失敗面的既有取捨，與
    //     `PresentationSink` 不吞失敗、忠實回報的精神一致）。
    DialogueStatus advance(const ds::profiles::PortraitProfile& speaker, ds::events::Tick ttl);

    // 以 dt 推進目前顯示中氣球的逐字進度 / 存活倒數（透傳 C1-03 `advance`）。未顯示中 no-op。
    void tick(ds::events::Tick dt);

    // --- 查詢 ---

    // 目前顯示中的一句台詞；未顯示中（尚未 start / 已 Finished / reset 後）回空字串。
    const std::string& current_line() const noexcept { return current_line_; }

    // 腳本自此已無更多台詞（自然跑到結尾）。
    bool is_finished() const noexcept { return finished_; }

    // 已 start 且尚未 Finished / reset（供呼叫端判斷可否呼叫 advance()）。
    bool is_active() const noexcept { return started_ && !finished_; }

    // 氣球目前是否顯示中（透傳 C1-03 `is_visible()`）。
    bool is_showing() const noexcept { return balloon_.is_visible(); }

    // 最近一次腳本執行錯誤（僅上一次呼叫回傳 ScriptError 後有效；成功呼叫不清空舊值，供事後
    // 除錯查驗，呼叫端應以回傳值判斷是否採信）。
    const ds::script::ScriptError& last_error() const noexcept { return last_error_; }

    // E8-03 表現層查詢（供呼叫端 / 測試查驗腳本中的舞台指示是否生效）。
    const ds::script::PresentationController& presentation() const noexcept { return *presentation_; }

    // --- 重置 ---

    // 收掉氣球（若顯示中）、捨棄目前執行進度（回到「未 start」狀態）。腳本定義本身保留（不需
    // 重新 load_script 即可再次 start()）。
    void reset();

private:
    // 內部：從目前執行位置跑到下一句台詞、腳本結束、或錯誤為止。跳過舞台指示（交給
    // LineSink 內部經 PresentationSink 分派）與其餘非 say 步驟（set / goto / if / label 由
    // Interpreter 自身消化，不會走到這裡）。
    DialogueStatus run_until_next_line();

    std::string id_;
    ds::profiles::BalloonProfile balloon_;  // C1-03：本對話本自身的氣球（surface id = id_）。

    ds::script::Script script_;  // E8-02：目前載入的腳本定義（load_script 覆寫，位址穩定）。
    bool has_script_ = false;

    // pimpl：隱藏「台詞 / 舞台指示分流」用的 OutputSink（LineSink）與其綁定的 E8-02
    // Interpreter（引用 script_，重新 start/load 需重建，故以 unique_ptr 持有）。完整定義於
    // dialogue_book.cpp。
    class LineSink;
    std::unique_ptr<LineSink> line_sink_;
    std::unique_ptr<ds::script::Interpreter> interpreter_;

    std::unique_ptr<ds::script::PresentationController> presentation_;  // E8-03：舞台指示註冊表。

    std::string current_line_;
    bool started_ = false;
    bool finished_ = false;
    ds::script::ScriptError last_error_{};
};

}  // namespace ds::content

#endif  // DS_CONTENT_C3_02_DIALOGUE_BOOK_HPP
