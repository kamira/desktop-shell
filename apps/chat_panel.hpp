// apps/c4_05/chat_panel.hpp — C4-05 AI 對話面板（artifact 層 / apps，相位 1）
//
// 「AI 對話面板」（聊天氣泡對話框）：顯示一段對話訊息串、可送出輸入。本單元不是新引擎
// 邏輯，而是把四個已合併的擴充點/元件**組裝**成單一應用：
//
//   - C1-05（`ds::profiles::SummonPanelProfile`）：本面板的召喚 / 收起生命週期 ——
//     `open()` 叫出面板（之後才可 `submit_input`）、`close()` 收起。本單元以參考持有，
//     不重造生命週期邏輯，純委派。
//   - E4-01（`ds::render::TextLayout` / `FontMetrics`）：文字排版；本單元亦直接用它量測
//     尚未送出的草稿輸入（`measure_draft()`），供輸入框即時預覽版面尺寸。
//   - E4-11（`ds::elements::TypewriterElement`）：對話串中每則訊息的**顯示元素**——逐字
//     顯示（typing effect），內部即消費 E4-01 對「目前已顯示前綴」排版。
//   - E2-14（`ds::sysinfo::HttpFetchProvider`）：上下文指標來源——本單元以參考持有一個
//     已由呼叫端註冊（`register_metrics`）的提供者，`refresh_context()` 委派其 `sample()`，
//     `context_value()` 讀目前指標快照（相位 1：`NullHttpTransport`，不發真實網路請求）。
//
// 行為組裝：`load_conversation(turns)` / `append_message(role, text)` / `submit_input(text)`
// （送出使用者輸入 → 附加使用者訊息 → 以**注入式**回應器產生助理回應並附加——相位 1 純資料
// 模型，**無真實 LLM 呼叫**）/ `scroll_to` / `scroll_by` / `scroll_to_latest`（依訊息序數捲動，
// 非絕對座標，NFR-02）。
//
// 相位 1（Mac / null 期）約束：純資料 / 邏輯組裝，無真實 GUI、無真實網路、無平台分支
// （無 `#ifdef` / win32 / cocoa）、無絕對座標 / 數字 z-order（NFR-02：訊息以序數
// `message_index` 定址——序數為資料序列位置，非畫面座標，沿用 E4-01 `LineBox::line` /
// E2-01 `Metric::instance(i)` 同款慣例）。任何無效操作（面板未開啟即送出、空白輸入、
// 索引越界）一律明確回傳具名結果，不靜默。
#ifndef DS_APPS_C4_05_CHAT_PANEL_HPP
#define DS_APPS_C4_05_CHAT_PANEL_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "http_fetch.hpp"           // E2-14（上游，可讀不可改）：HttpFetchProvider / MetricValue
#include "summon_panel_profile.hpp"  // C1-05（上游，可讀不可改）：SummonPanelProfile
#include "typewriter_element.hpp"    // E4-11（上游，可讀不可改）：TypewriterElement
                                      //   （透傳 E4-01 TextLayout/FontMetrics/LayoutConstraints/
                                      //    LayoutResult/Size，與 E4-09 AnimationDriver/Tick）

namespace ds::apps {

// 訊息角色（具名，NFR-02）。
enum class MessageRole {
    User,       // 使用者輸入。
    Assistant,  // AI 助理回應（相位 1：注入式 stub，非真實 LLM 呼叫）。
    System,     // 系統訊息（如載入公告）。
};

const char* to_string(MessageRole r) noexcept;

// submit_input() 的具名結果。
enum class SubmitStatus {
    Ok,           // 成功：使用者訊息 + 助理回應（stub）皆已附加。
    Empty,        // 輸入去除首尾空白後為空：no-op，不附加任何訊息。
    PanelClosed,  // 面板未開啟（委派 C1-05 `is_open()`）：拒絕送出，no-op。
};

const char* to_string(SubmitStatus s) noexcept;

// 一則對話 turn：角色 + 文字，供 `load_conversation()` 批次載入。
using ConversationTurn = std::pair<MessageRole, std::string>;

// 一則訊息的查詢快照（不含所有權，供呼叫端讀取顯示狀態）。
struct ChatMessageView {
    MessageRole role = MessageRole::User;
    std::string text;             // 完整原始文字（送出時的原文，未經逐字顯示裁切）。
    std::size_t visible_count = 0;  // 目前已顯示（E4-11 逐字顯示）碼位數。
    std::size_t total_count = 0;    // 完整文字碼位數。
    bool reveal_complete = false;   // 是否已顯示完整段文字。
};

// 相位 1 預設逐字顯示速度（每 tick 幾個字元）；沿用 E4-11 預設。
inline constexpr double kDefaultRevealSpeed = 1.0;

// 送出使用者輸入後，產生助理回應文字的**注入式**回應器（相位 1：無真實 LLM 呼叫）。
// 參數：使用者原文、本次為第幾輪送出（0-based，自本物件建構起累計）。
using ResponderFn = std::function<std::string(const std::string& user_text,
                                               std::size_t turn_index)>;

// ---------------------------------------------------------------------------
// ChatPanelApp —— AI 對話面板：組裝 C1-05（生命週期）+ E4-01（排版）+ E4-11（逐字顯示）
// + E2-14（上下文指標）。
//
// 注入式相依（皆不取得所有權，須比本物件活得久）：
//   - `SummonPanelProfile&`（C1-05）：面板召喚 / 收起。
//   - `FontMetrics&`（E4-01）：所有訊息與草稿量測共用同一字型度量。
//   - `HttpFetchProvider&`（E2-14）：上下文指標來源；呼叫端須先對某 `MetricRegistry`
//     呼叫其 `register_metrics()`，本物件只消費、不重複註冊。
// ---------------------------------------------------------------------------
class ChatPanelApp {
public:
    ChatPanelApp(ds::profiles::SummonPanelProfile& base, const ds::render::FontMetrics& metrics,
                 ds::sysinfo::HttpFetchProvider& context_provider,
                 ds::render::LayoutConstraints constraints = {},
                 ds::kernel::SurfaceId surface = {});

    // --- C1-05 面板生命週期（純委派）---
    bool open(ds::events::Tick ttl) { return base_.open(ttl); }
    bool close() { return base_.close(); }
    bool is_open() const noexcept { return base_.is_open(); }

    // --- 對話載入 / 附加 ---

    // 以一批 turns 取代目前整段對話：清空既有訊息，逐一重建 E4-11 顯示元素（各自從頭開始
    // 逐字顯示），捲動歸底（`scroll_to_latest()`）。非法 UTF-8 文字 → std::invalid_argument
    // （沿用 E4-01 `decode_utf8` 驗證，不靜默；擲出前已清空的訊息**不**回復——呼叫端應
    // 確保輸入合法）。
    void load_conversation(const std::vector<ConversationTurn>& turns);

    // 附加一則訊息（角色 + 文字）。不需面板開啟（對話資料獨立於面板顯隱）。若目前捲動
    // 停留在最新訊息（`auto_scroll()`），捲動隨新訊息跟進；否則保留使用者目前捲動位置
    // （避免打斷正在回顧歷史的使用者）。若已綁定動畫驅動源（`attach()`），新訊息自動
    // 一併掛上。非法 UTF-8 → std::invalid_argument（不靜默）。
    void append_message(MessageRole role, const std::string& text);

    // --- 送出輸入（相位 1：注入式回應，無真實 LLM 呼叫）---

    // 送出使用者輸入：
    //   - 面板未開啟（`!is_open()`）→ `PanelClosed`，no-op。
    //   - 去除首尾 ASCII 空白後為空 → `Empty`，no-op（不附加任何訊息）。
    //   - 否則：附加一則 `User` 訊息（原文，未去除空白）→ 呼叫回應器（`set_responder()`
    //     設定；未設定則用內建 stub）產生回應文字 → 附加一則 `Assistant` 訊息。回傳 `Ok`。
    SubmitStatus submit_input(const std::string& text);

    // 設定回應產生器；未設定則使用內建預設 stub（可預期樣板，供測試 / 開發期預覽，
    // **非**真實 LLM 呼叫）。
    void set_responder(ResponderFn fn);

    // --- 顯示推進（E4-11 逐字顯示，注入式 tick）---

    // 對所有訊息推進逐字顯示（已完成者為安全 no-op，沿用 E4-11 語意）。
    void advance(ds::render::Tick dt);

    // 綁一個 E4-09 動畫驅動源：目前所有訊息與之後新附加的訊息，其逐字顯示改由該驅動源
    // 心跳自動推進（委派 E4-11 `attach()`）。驅動源壽命須涵蓋本物件。
    void attach(ds::render::AnimationDriver& driver);

    // --- 捲動（依訊息序數，非絕對座標，NFR-02）---

    // 捲到指定訊息序數：空對話或索引越界 → false（no-op）；成功 → true，並依是否捲到
    // 最新訊息更新 `auto_scroll()`。
    bool scroll_to(std::size_t message_index);

    // 相對移動（可負）：結果夾在 `[0, message_count()-1]`。空對話 → false（no-op）；
    // 否則恆成功（含夾到邊界的情形）。
    bool scroll_by(std::ptrdiff_t delta);

    // 捲到最新訊息（空對話則索引維持 0）；恆成功，並重新開啟 `auto_scroll()`。
    void scroll_to_latest();

    std::size_t scroll_index() const noexcept { return scroll_index_; }
    bool auto_scroll() const noexcept { return auto_scroll_; }

    // --- E2-14 上下文指標 ---

    // 觸發一次上下文指標採集（委派 `HttpFetchProvider::sample()`）。回傳本次是否取得
    // 有效數值（相位 1：`NullHttpTransport` 未注入回應時恆 false，不崩）。
    bool refresh_context();

    // 目前上下文指標快照；提供者尚未 register 或從未成功採集 → `valid == false`。
    ds::metrics::MetricValue context_value() const;

    // --- 查詢 ---

    std::size_t message_count() const noexcept { return messages_.size(); }
    bool empty() const noexcept { return messages_.empty(); }

    // 第 index 則訊息的查詢快照。越界 → std::out_of_range。
    ChatMessageView message_at(std::size_t index) const;

    // 第 index 則訊息目前的渲染描述（E4-01 排版 + E4-11 逐字顯示前綴）。越界 →
    // std::out_of_range。
    ds::render::LayoutResult render_message(std::size_t index) const;

    // 量測一段尚未送出的草稿輸入（E4-01 `TextLayout::measure`，供輸入框即時預覽版面
    // 尺寸）；非法 UTF-8 → std::invalid_argument（沿用 E4-01）。
    ds::render::Size measure_draft(const std::string& text) const;

private:
    struct MessageEntry {
        MessageRole role;
        std::string text;
        ds::elements::TypewriterElement display;

        MessageEntry(MessageRole r, std::string t, ds::elements::TypewriterElement d)
            : role(r), text(std::move(t)), display(std::move(d)) {}
    };

    // 建構並附加一則訊息（供 append_message / load_conversation 共用）。
    void push_message(MessageRole role, const std::string& text);

    ds::profiles::SummonPanelProfile& base_;
    const ds::render::FontMetrics& metrics_;
    ds::sysinfo::HttpFetchProvider& context_provider_;
    ds::render::LayoutConstraints constraints_;
    ds::kernel::SurfaceId surface_;

    // 以 unique_ptr 持有每則訊息：`TypewriterElement::attach()`（E4-11/E4-09）在 driver 內
    // 以 lambda 捕捉 `this`，故各訊息的位址須於其存活期間穩定——若直接以值存於
    // `std::vector<MessageEntry>`，附加新訊息造成的 vector 重新配置會搬動既有元素、使已
    // 綁定動畫的捕捉位址懸空。改以 `vector<unique_ptr<MessageEntry>>`：重新配置只搬動指標
    // 本身，各 `MessageEntry`（連同其 `TypewriterElement`）的堆積位址恆不變。
    std::vector<std::unique_ptr<MessageEntry>> messages_;
    std::size_t scroll_index_ = 0;
    bool auto_scroll_ = true;

    double reveal_speed_ = kDefaultRevealSpeed;
    ds::render::AnimationDriver* driver_ = nullptr;  // 不擁有；nullptr = 未綁定。

    ResponderFn responder_;
    std::size_t submit_turn_ = 0;
};

}  // namespace ds::apps

#endif  // DS_APPS_C4_05_CHAT_PANEL_HPP
