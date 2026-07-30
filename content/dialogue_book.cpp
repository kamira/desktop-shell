// content/c3_02/dialogue_book.cpp — C3-02 角色對話本實作（組裝型 artifact 單元）
//
// 台詞 / 舞台指示分流：`LineSink`（本檔內部 pimpl 類別，`DialogueBook::LineSink`）實作 E8-02
// `OutputSink`——每次 `say` 步驟 emit 一行文字時，先檢查其第一個 token 是否為五個已知舞台指示
// 名稱之一（`show` / `hide` / `switch_surface` / `transition` / `wait`，見
// presentation_commands.hpp 檔首「文字格式」一節）：若是，直接轉交內部 E8-03 `PresentationSink`
// 經注入的 CommandBus 分派（不算一句台詞，`advance()` 會略過繼續尋找下一句）；否則視為一句
// 真正的台詞，記為待顯示（`pending_line_`），供 `run_until_next_line()` 取出交給 C1-03 氣球。
//
// 相位 1：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 #ifdef / win32 / cocoa）、無絕對座標 /
// 數字 z-order（NFR-02）。腳本執行期錯誤 / 顯示失敗一律結構化回報，不靜默。
#include "dialogue_book.hpp"

#include <utility>  // std::move

namespace ds::content {

const char* to_string(DialogueStatus s) noexcept {
    switch (s) {
        case DialogueStatus::Ok:
            return "Ok";
        case DialogueStatus::Invalid:
            return "Invalid";
        case DialogueStatus::ScriptError:
            return "ScriptError";
        case DialogueStatus::Finished:
            return "Finished";
    }
    return "unknown";
}

// -----------------------------------------------------------------------------
// LineSink —— 台詞 / 舞台指示分流用的 E8-02 OutputSink。
// -----------------------------------------------------------------------------
class DialogueBook::LineSink : public ds::script::OutputSink {
public:
    // bus：轉交內部 E8-03 PresentationSink（不取得所有權；生命週期須涵蓋本物件，與
    // DialogueBook 對 bus 的要求一致）。
    explicit LineSink(ds::command::CommandBus& bus) : presentation_sink_(bus) {}

    // 見檔頭說明：先判斷是否為舞台指示，否則記為待顯示的台詞。
    void emit(const std::string& text) override {
        if (is_presentation_directive(text)) {
            presentation_sink_.emit(text);
            return;
        }
        pending_line_ = text;
        has_pending_ = true;
    }

    bool has_pending_line() const noexcept { return has_pending_; }

    // 取出並清空待顯示台詞（呼叫端須先以 has_pending_line() 確認有值）。
    std::string take_pending_line() {
        has_pending_ = false;
        return std::move(pending_line_);
    }

    void clear_pending() noexcept {
        has_pending_ = false;
        pending_line_.clear();
    }

private:
    // 第一個以空白分隔的 token 若為五個已知舞台指示名稱之一 → 視為舞台指示（與
    // presentation_commands.hpp 的文字格式一致；不需重複其參數解析，僅需判斷「是不是」）。
    static bool is_presentation_directive(const std::string& text) {
        static const char* const kNames[] = {"show", "hide", "switch_surface", "transition", "wait"};
        const auto space = text.find(' ');
        const std::string head = (space == std::string::npos) ? text : text.substr(0, space);
        for (const char* name : kNames) {
            if (head == name) {
                return true;
            }
        }
        return false;
    }

    ds::script::PresentationSink presentation_sink_;
    bool has_pending_ = false;
    std::string pending_line_;
};

// -----------------------------------------------------------------------------
// DialogueBook
// -----------------------------------------------------------------------------

DialogueBook::DialogueBook(std::string id, const ds::render::FontMetrics& metrics,
                           ds::command::CommandBus& bus, ds::render::SurfaceSwitcher& surfaces,
                           ds::render::LayoutConstraints constraints)
    : id_(std::move(id)),
      balloon_(id_, metrics, constraints),
      line_sink_(std::make_unique<LineSink>(bus)),
      presentation_(std::make_unique<ds::script::PresentationController>(surfaces, bus)) {}

// unique_ptr 成員（LineSink / Interpreter / PresentationController）解構需要完整型別，
// 三者於本檔（或本檔已 include 的標頭）皆已完整定義，故可於此以 = default 收尾。
DialogueBook::~DialogueBook() = default;

bool DialogueBook::ready() const noexcept { return presentation_->ready(); }

const std::string& DialogueBook::registration_error() const noexcept {
    return presentation_->registration_error();
}

DialogueStatus DialogueBook::load_script(const ds::script::Script& script) {
    if (script.empty()) {
        return DialogueStatus::Invalid;  // 空腳本：不建立可跑狀態
    }
    reset();  // 捨棄既有播放進度（腳本即將被替換）
    script_ = script;
    has_script_ = true;
    return DialogueStatus::Ok;
}

DialogueStatus DialogueBook::start(const ds::profiles::PortraitProfile& speaker, ds::events::Tick ttl) {
    if (!has_script_) {
        return DialogueStatus::Invalid;  // 尚未 load_script
    }
    if (!ready()) {
        return DialogueStatus::Invalid;  // 舞台指示未成功掛上 bus，不靜默用半殘的表現層
    }
    if (started_ && !finished_) {
        return DialogueStatus::Invalid;  // 已在播放中，不靜默重播；先 reset() 再重新開始
    }

    line_sink_->clear_pending();
    interpreter_ = std::make_unique<ds::script::Interpreter>(script_, *line_sink_);
    started_ = true;
    finished_ = false;
    current_line_.clear();

    DialogueStatus status = run_until_next_line();
    if (status != DialogueStatus::Ok) {
        return status;  // ScriptError 或 Finished（finished_ 已於 run_until_next_line 內設定）
    }
    if (balloon_.show_balloon(speaker, current_line_, ttl) != ds::profiles::BalloonStatus::Ok) {
        // 顯示失敗（如 speaker 未載入）：腳本執行位置已前進到這句台詞之後，見標頭說明；
        // 呼叫端須 reset() 後以有效 speaker 重試。
        return DialogueStatus::Invalid;
    }
    return DialogueStatus::Ok;
}

DialogueStatus DialogueBook::advance(const ds::profiles::PortraitProfile& speaker, ds::events::Tick ttl) {
    if (!started_ || finished_) {
        return DialogueStatus::Invalid;  // 尚未 start，或已 Finished / reset 後未重新 start
    }

    if (balloon_.is_visible()) {
        balloon_.dismiss();  // 收掉目前顯示中的一句，準備顯示下一句
    }

    DialogueStatus status = run_until_next_line();
    if (status != DialogueStatus::Ok) {
        return status;
    }
    if (balloon_.show_balloon(speaker, current_line_, ttl) != ds::profiles::BalloonStatus::Ok) {
        return DialogueStatus::Invalid;
    }
    return DialogueStatus::Ok;
}

void DialogueBook::tick(ds::events::Tick dt) { balloon_.advance(dt); }

void DialogueBook::reset() {
    if (balloon_.is_visible()) {
        balloon_.dismiss();
    }
    interpreter_.reset();
    if (line_sink_) {
        line_sink_->clear_pending();
    }
    started_ = false;
    finished_ = false;
    current_line_.clear();
}

DialogueStatus DialogueBook::run_until_next_line() {
    line_sink_->clear_pending();
    while (!interpreter_->finished()) {
        ds::script::StepResult res = interpreter_->step();
        if (!res) {
            last_error_ = res.error();
            finished_ = true;  // 執行期錯誤視為終止：需 reset() 才能重新 start/advance
            current_line_.clear();
            return DialogueStatus::ScriptError;
        }
        if (line_sink_->has_pending_line()) {
            current_line_ = line_sink_->take_pending_line();
            return DialogueStatus::Ok;
        }
    }
    finished_ = true;
    current_line_.clear();
    return DialogueStatus::Finished;
}

}  // namespace ds::content
