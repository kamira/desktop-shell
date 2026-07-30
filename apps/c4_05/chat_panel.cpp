// apps/c4_05/chat_panel.cpp — C4-05 AI 對話面板實作
//
// 純組裝邏輯：委派 C1-05 `SummonPanelProfile` 的召喚 / 收起、以 E4-11 `TypewriterElement`
// （內部消費 E4-01 `TextLayout`）呈現每則訊息的逐字顯示、委派 E2-14 `HttpFetchProvider`
// 的上下文指標採集。無平台分支、無真實網路、無真實 LLM 呼叫（相位 1：回應為注入式 stub）。
#include "chat_panel.hpp"

#include <cctype>
#include <stdexcept>
#include <utility>

namespace ds::apps {

const char* to_string(MessageRole r) noexcept {
    switch (r) {
        case MessageRole::User:
            return "User";
        case MessageRole::Assistant:
            return "Assistant";
        case MessageRole::System:
            return "System";
    }
    return "unknown";
}

const char* to_string(SubmitStatus s) noexcept {
    switch (s) {
        case SubmitStatus::Ok:
            return "Ok";
        case SubmitStatus::Empty:
            return "Empty";
        case SubmitStatus::PanelClosed:
            return "PanelClosed";
    }
    return "unknown";
}

namespace {

// 去除首尾 ASCII 空白（空格 / tab / 換行 / 回車）；不處理多位元組空白（相位 1 夠用，
// 沿用其餘單元對「輸入是否為空」的最小判斷慣例）。
std::string trim_ascii(const std::string& s) {
    std::size_t begin = 0;
    while (begin < s.size() &&
           std::isspace(static_cast<unsigned char>(s[begin])) != 0) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(begin, end - begin);
}

// 相位 1 內建預設回應器：**非**真實 LLM 呼叫，回可預期樣板文字，供測試 / 開發期預覽。
std::string default_responder(const std::string& user_text, std::size_t turn_index) {
    return "[stub#" + std::to_string(turn_index) + "] 收到: " + user_text;
}

}  // namespace

ChatPanelApp::ChatPanelApp(ds::profiles::SummonPanelProfile& base,
                           const ds::render::FontMetrics& metrics,
                           ds::sysinfo::HttpFetchProvider& context_provider,
                           ds::render::LayoutConstraints constraints,
                           ds::kernel::SurfaceId surface)
    : base_(base),
      metrics_(metrics),
      context_provider_(context_provider),
      constraints_(std::move(constraints)),
      surface_(std::move(surface)),
      responder_(default_responder) {}

void ChatPanelApp::push_message(MessageRole role, const std::string& text) {
    // 先以 make_unique 建構最終位址穩定的 MessageEntry，再對其成員（而非中繼區域變數）
    // 呼叫 set_speed / set_text / attach——確保 attach() 捕捉到的 `this` 是本訊息恆穩定的
    // 堆積位址（見標頭 messages_ 欄位註解）。
    auto entry = std::make_unique<MessageEntry>(
        role, text, ds::elements::TypewriterElement(metrics_, constraints_, surface_));
    entry->display.set_speed(reveal_speed_);
    entry->display.set_text(text);  // 非法 UTF-8 → std::invalid_argument（沿用 E4-01，不靜默）。
    if (driver_ != nullptr) {
        entry->display.attach(*driver_);
    }
    messages_.push_back(std::move(entry));

    if (auto_scroll_) {
        scroll_index_ = messages_.size() - 1;
    }
}

void ChatPanelApp::load_conversation(const std::vector<ConversationTurn>& turns) {
    messages_.clear();
    messages_.reserve(turns.size());
    // 重新載入視為「回到底部」：先開啟 auto_scroll 再逐一附加（每則附加皆會跟進到底）。
    auto_scroll_ = true;
    scroll_index_ = 0;
    submit_turn_ = 0;
    for (const auto& turn : turns) {
        push_message(turn.first, turn.second);
    }
}

void ChatPanelApp::append_message(MessageRole role, const std::string& text) {
    push_message(role, text);
}

SubmitStatus ChatPanelApp::submit_input(const std::string& text) {
    if (!base_.is_open()) {
        return SubmitStatus::PanelClosed;  // 面板未開啟：拒絕送出，no-op。
    }
    if (trim_ascii(text).empty()) {
        return SubmitStatus::Empty;  // 空白輸入：no-op，不附加任何訊息。
    }

    append_message(MessageRole::User, text);  // 保留原文（含前後空白），不擅自改寫輸入。

    const std::size_t turn = submit_turn_++;
    const std::string reply = responder_ ? responder_(text, turn) : default_responder(text, turn);
    append_message(MessageRole::Assistant, reply);
    return SubmitStatus::Ok;
}

void ChatPanelApp::set_responder(ResponderFn fn) {
    responder_ = fn ? std::move(fn) : ResponderFn(default_responder);
}

void ChatPanelApp::advance(ds::render::Tick dt) {
    for (auto& entry : messages_) {
        entry->display.advance(dt);  // 已完成者為安全 no-op（沿用 E4-11 語意）。
    }
}

void ChatPanelApp::attach(ds::render::AnimationDriver& driver) {
    driver_ = &driver;
    for (auto& entry : messages_) {
        entry->display.attach(driver);
    }
}

bool ChatPanelApp::scroll_to(std::size_t message_index) {
    if (messages_.empty() || message_index >= messages_.size()) {
        return false;  // 空對話或索引越界：no-op，不靜默。
    }
    scroll_index_ = message_index;
    auto_scroll_ = (message_index + 1 == messages_.size());
    return true;
}

bool ChatPanelApp::scroll_by(std::ptrdiff_t delta) {
    if (messages_.empty()) {
        return false;
    }
    const std::ptrdiff_t last = static_cast<std::ptrdiff_t>(messages_.size()) - 1;
    std::ptrdiff_t next = static_cast<std::ptrdiff_t>(scroll_index_) + delta;
    if (next < 0) {
        next = 0;
    } else if (next > last) {
        next = last;
    }
    scroll_index_ = static_cast<std::size_t>(next);
    auto_scroll_ = (static_cast<std::ptrdiff_t>(scroll_index_) == last);
    return true;
}

void ChatPanelApp::scroll_to_latest() {
    scroll_index_ = messages_.empty() ? 0 : messages_.size() - 1;
    auto_scroll_ = true;
}

bool ChatPanelApp::refresh_context() { return context_provider_.sample(); }

ds::metrics::MetricValue ChatPanelApp::context_value() const {
    std::shared_ptr<ds::metrics::Metric> metric = context_provider_.metric();
    if (!metric) {
        return ds::metrics::MetricValue::unknown();  // 尚未 register：明確回無讀值，不崩。
    }
    return metric->single().value();
}

ChatMessageView ChatPanelApp::message_at(std::size_t index) const {
    const MessageEntry& entry = *messages_.at(index);  // 越界 → std::out_of_range。
    ChatMessageView view;
    view.role = entry.role;
    view.text = entry.text;
    view.visible_count = entry.display.visible_count();
    view.total_count = entry.display.total_count();
    view.reveal_complete = entry.display.is_complete();
    return view;
}

ds::render::LayoutResult ChatPanelApp::render_message(std::size_t index) const {
    return messages_.at(index)->display.render_model();  // 越界 → std::out_of_range。
}

ds::render::Size ChatPanelApp::measure_draft(const std::string& text) const {
    ds::render::TextLayout layout(metrics_, surface_);
    return layout.measure(text, constraints_);  // 非法 UTF-8 → std::invalid_argument（沿用 E4-01）。
}

}  // namespace ds::apps
