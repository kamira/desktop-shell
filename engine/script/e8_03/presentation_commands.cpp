// E8-03 表現控制指令集 — 實作（見 presentation_commands.hpp 規格）。
#include "presentation_commands.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ds::script {

namespace {

// 從 CommandArgs 讀一個數值型參數：接受 Double 或 Int（自動轉 double）；缺鍵 / 型別不符
// （如收到 String）一律回 std::nullopt——由呼叫端統一視為「缺參數」轉 Invalid，不特判。
std::optional<double> numeric_arg(const CommandArgs& args, const std::string& key) {
    if (auto d = args.get_double(key)) {
        return d;
    }
    if (auto i = args.get_int(key)) {
        return static_cast<double>(*i);
    }
    return std::nullopt;
}

bool finite_and_nonnegative(double v) { return std::isfinite(v) && v >= 0.0; }

// 把整段字串解析為 double；必須整段皆為數值（無殘留字元），否則視為解析失敗。
std::optional<double> parse_double_strict(const std::string& token) {
    if (token.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        double value = std::stod(token, &consumed);
        if (consumed != token.size()) {
            return std::nullopt;  // 有殘留字元（如 "1.0abc"）：不是純數值 token
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;  // 無法解析（如空字串以外的非數值文字）：不崩潰、回 nullopt
    }
}

// 以空白切分文字為 token 序列（phase 1 最小語法：不支援引號 / 逃逸）。
std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

// 安全取第 idx 個 token；越界回 std::nullopt（不丟例外、不 UB）。
std::optional<std::string> token_at(const std::vector<std::string>& tokens, std::size_t idx) {
    if (idx >= tokens.size()) {
        return std::nullopt;
    }
    return tokens[idx];
}

}  // namespace

// -----------------------------------------------------------------------------
// PresentationController
// -----------------------------------------------------------------------------

PresentationController::PresentationController(SurfaceSwitcher& surfaces, CommandBus& bus)
    : surfaces_(surfaces), bus_(bus) {
    registration_ok_ = true;

    auto try_register = [&](const CommandId& id, ds::command::CommandHandler handler) {
        if (!registration_ok_) {
            return;  // 已有一個失敗；維持第一個失敗原因，不繼續嘗試（不部分套用地假裝成功）。
        }
        if (!register_one(id, std::move(handler))) {
            registration_ok_ = false;
            registration_error_ = "PresentationController: 命令 id 已被佔用，註冊失敗: " + id;
        }
    };

    try_register(presentation_commands::kShow,
                 [this](const CommandArgs& a) { return handle_show(a); });
    try_register(presentation_commands::kHide,
                 [this](const CommandArgs& a) { return handle_hide(a); });
    try_register(presentation_commands::kSwitchSurface,
                 [this](const CommandArgs& a) { return handle_switch_surface(a); });
    try_register(presentation_commands::kTransition,
                 [this](const CommandArgs& a) { return handle_transition(a); });
    try_register(presentation_commands::kWait,
                 [this](const CommandArgs& a) { return handle_wait(a); });
}

PresentationController::~PresentationController() {
    for (const auto& id : registered_ids_) {
        bus_.unregister(id);
    }
}

bool PresentationController::register_one(const CommandId& id,
                                           ds::command::CommandHandler handler) {
    if (!bus_.register_command(id, std::move(handler))) {
        return false;
    }
    registered_ids_.push_back(id);
    return true;
}

bool PresentationController::is_visible(const std::string& target) const {
    auto it = visibility_.find(target);
    return it != visibility_.end() && it->second;
}

std::size_t PresentationController::visible_count() const {
    std::size_t n = 0;
    for (const auto& kv : visibility_) {
        if (kv.second) {
            ++n;
        }
    }
    return n;
}

CommandResult PresentationController::handle_show(const CommandArgs& args) {
    auto target = args.get_string("target");
    if (!target || target->empty()) {
        return CommandResult::make_failed("show: missing/empty 'target'");
    }
    visibility_[*target] = true;
    return CommandResult::make_ok(*target, "shown: " + *target);
}

CommandResult PresentationController::handle_hide(const CommandArgs& args) {
    auto target = args.get_string("target");
    if (!target || target->empty()) {
        return CommandResult::make_failed("hide: missing/empty 'target'");
    }
    visibility_[*target] = false;
    return CommandResult::make_ok(*target, "hidden: " + *target);
}

CommandResult PresentationController::handle_switch_surface(const CommandArgs& args) {
    auto target = args.get_string("target");
    if (!target || target->empty()) {
        return CommandResult::make_failed("switch_surface: missing/empty 'target'");
    }
    SwitchStatus status = surfaces_.switch_to(*target);
    if (status == SwitchStatus::Ok) {
        return CommandResult::make_ok(*target, "switched to: " + *target);
    }
    // SurfaceSwitcher::switch_to 的既有契約只回 Ok / NotFound（見 surface_switcher.hpp）。
    return CommandResult::make_failed("switch_surface: unknown surface: " + *target);
}

CommandResult PresentationController::handle_transition(const CommandArgs& args) {
    auto kind = args.get_string("kind");
    auto from = args.get_string("from");
    auto to = args.get_string("to");
    auto duration = numeric_arg(args, "duration");

    if (!kind || kind->empty()) {
        return CommandResult::make_failed("transition: missing/empty 'kind'");
    }
    if (!from || from->empty()) {
        return CommandResult::make_failed("transition: missing/empty 'from'");
    }
    if (!to || to->empty()) {
        return CommandResult::make_failed("transition: missing/empty 'to'");
    }
    if (!duration || !finite_and_nonnegative(*duration)) {
        return CommandResult::make_failed(
            "transition: missing/invalid 'duration' (must be a finite number >= 0)");
    }

    transitions_.push_back(TransitionRecord{*kind, *from, *to, *duration});
    return CommandResult::make_ok(
        *kind, "transition " + *kind + ": " + *from + " -> " + *to);
}

CommandResult PresentationController::handle_wait(const CommandArgs& args) {
    auto seconds = numeric_arg(args, "seconds");
    if (!seconds || !finite_and_nonnegative(*seconds)) {
        return CommandResult::make_failed(
            "wait: missing/invalid 'seconds' (must be a finite number >= 0)");
    }
    waits_.push_back(WaitRecord{*seconds});
    return CommandResult::make_ok(*seconds, "waited");
}

// -----------------------------------------------------------------------------
// PresentationSink — E8-02 整合點
// -----------------------------------------------------------------------------

void PresentationSink::emit(const std::string& text) {
    std::vector<std::string> tokens = tokenize(text);

    // 空白 / 空行：視為未知指令（id 為空字串，bus 上必然未註冊）→ 交由 bus 回 NotFound，
    // 不特判、不靜默丟棄。
    std::string name = tokens.empty() ? std::string{} : tokens[0];

    ds::command::CommandId id;
    CommandArgs command_args;

    if (name == "show") {
        id = presentation_commands::kShow;
        if (auto t = token_at(tokens, 1)) {
            command_args.set("target", *t);
        }
    } else if (name == "hide") {
        id = presentation_commands::kHide;
        if (auto t = token_at(tokens, 1)) {
            command_args.set("target", *t);
        }
    } else if (name == "switch_surface") {
        id = presentation_commands::kSwitchSurface;
        if (auto t = token_at(tokens, 1)) {
            command_args.set("target", *t);
        }
    } else if (name == "transition") {
        id = presentation_commands::kTransition;
        if (auto k = token_at(tokens, 1)) {
            command_args.set("kind", *k);
        }
        if (auto f = token_at(tokens, 2)) {
            command_args.set("from", *f);
        }
        if (auto t = token_at(tokens, 3)) {
            command_args.set("to", *t);
        }
        if (auto d = token_at(tokens, 4)) {
            if (auto parsed = parse_double_strict(*d)) {
                command_args.set("duration", *parsed);
            } else {
                command_args.set("duration", *d);  // 非數值：原樣帶入字串，交由處理器判 Invalid
            }
        }
    } else if (name == "wait") {
        id = presentation_commands::kWait;
        if (auto s = token_at(tokens, 1)) {
            if (auto parsed = parse_double_strict(*s)) {
                command_args.set("seconds", *parsed);
            } else {
                command_args.set("seconds", *s);
            }
        }
    } else {
        // 未知指令名稱：組一個必然未在 bus 上註冊的 id，沿用 CommandBus 既有的
        // 「未知命令 → NotFound」契約（command_bus.hpp），不另寫特判邏輯。
        id = name.empty() ? std::string{} : ("ds::script." + name);
    }

    results_.push_back(bus_.dispatch(id, command_args));
}

bool PresentationSink::all_ok() const noexcept {
    for (const auto& r : results_) {
        if (!r.ok()) {
            return false;
        }
    }
    return true;
}

std::size_t PresentationSink::failure_count() const noexcept {
    std::size_t n = 0;
    for (const auto& r : results_) {
        if (!r.ok()) {
            ++n;
        }
    }
    return n;
}

}  // namespace ds::script
