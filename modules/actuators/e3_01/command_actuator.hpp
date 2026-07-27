// E3-01 外部命令執行致動器 — 平台中立契約（相位 1：介面 + null 執行器）
//
// 語意：把「執行外部命令 / 行程」這種副作用，以具名命令掛上 E6-01 命令匯流排
// （`command.run`）。呼叫端只需 命令 id + 具名參數（命令列 / 參數 / 工作目錄 / 環境）
// 即可觸發，不需相依本致動器或任何 OS API。
//
// 分層 / 相位：本單元屬 modules/actuators（動作層），消費 E6-01 契約。
//   - 相位 1（Mac / null 期）：**絕不真的 spawn 行程 / 呼叫 system / exec / fork /
//     CreateProcess**。所有請求交由可抽換的 `CommandRunner` 承接；預設 `NullCommandRunner`
//     只把請求「記錄」下來並回可注入的結果（預設 no-op 成功），絕不觸碰 OS。
//     相位 2 換上真實後端（win32 / cocoa）時，本致動器與命令契約一行不動。
//   - 無 `#ifdef` / 平台分支 / 真實 exec / spawn / fork / system；唯一 `#ifndef` 為 header guard。
//
// 因此可完全以單元測試驗證：命令註冊到匯流排、dispatch 觸發執行器、具名參數（program /
// args / cwd / env）傳遞、注入成功 / 失敗（非零 exit）結果回報、null 執行器 no-op、
// 必填參數缺漏回結構化失敗（不崩潰）。
#ifndef DS_ACTUATORS_E3_01_COMMAND_ACTUATOR_HPP
#define DS_ACTUATORS_E3_01_COMMAND_ACTUATOR_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "command_bus.hpp"  // E6-01：重用命令匯流排 / 穩定值型別 / 具名命令（PUBLIC 相依）

namespace ds::actuators {

// 擴充點契約版本標記（承重：呼叫端 / 相位 2 後端消費）。定義在 .cpp。
const char* command_actuator_contract_version() noexcept;

// 具名命令 id（穩定、可讀字串，不使用數字 opcode；與 E6-01 CommandId 取捨一致）。
inline constexpr const char* kCmdCommandRun = "command.run";

// ---------------------------------------------------------------------------
// EnvVar / CommandSpec — 平台中立地描述一次「執行外部命令」意圖。
//
// 不含任何 OS handle / process handle 假設；只承載呼叫端表達的可執行檔、命令列參數、
// 工作目錄與環境變數，讓相位 2 的真實執行器（或相位 1 的 null 執行器）自行決定如何實現。
// ---------------------------------------------------------------------------
struct EnvVar {
    std::string name;
    std::string value;

    bool operator==(const EnvVar& o) const { return name == o.name && value == o.value; }
    bool operator!=(const EnvVar& o) const { return !(*this == o); }
};

struct CommandSpec {
    std::string program;                 // 可執行檔名 / 路徑（必填、非空）
    std::vector<std::string> arguments;  // 命令列參數（有序、選用）
    std::string working_directory;       // 工作目錄（選用；空表示由執行器決定）
    std::vector<EnvVar> environment;     // 追加 / 覆寫的環境變數（選用）

    bool operator==(const CommandSpec& o) const {
        return program == o.program && arguments == o.arguments &&
               working_directory == o.working_directory && environment == o.environment;
    }
    bool operator!=(const CommandSpec& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// RunResult — 一次執行的結果（平台中立）。
//
// exit_code 依 POSIX 慣例：0 = 成功，非零 = 失敗。stdout / stderr 為擷取到的輸出。
// 相位 1 由注入 / null 執行器產生；相位 2 由真實執行器從實際行程收集。
// ---------------------------------------------------------------------------
struct RunResult {
    int exit_code = 0;
    std::string stdout_text;
    std::string stderr_text;

    bool success() const noexcept { return exit_code == 0; }

    bool operator==(const RunResult& o) const {
        return exit_code == o.exit_code && stdout_text == o.stdout_text &&
               stderr_text == o.stderr_text;
    }
    bool operator!=(const RunResult& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// CommandRunner — 執行實際副作用的抽象執行器。
//
// 相位 1 僅提供 NullCommandRunner；相位 2 由平台後端實作 run() 真的 spawn 行程。
// run() 回 RunResult，讓致動器把它轉為 E6-01 CommandResult 交回匯流排呼叫端。
// ---------------------------------------------------------------------------
class CommandRunner {
public:
    virtual ~CommandRunner() = default;
    virtual RunResult run(const CommandSpec& spec) = 0;
};

// ---------------------------------------------------------------------------
// NullCommandRunner — 相位 1 預設執行器：不觸碰 OS，只「記錄」每個請求並回注入結果。
//
// 讓致動器在無真實平台後端時仍可完整跑通（註冊 → 分派 → 記錄 → 回報），並讓測試 /
// 診斷驗證「呼叫端到底請求了什麼」。可注入固定 RunResult（模擬成功 / 非零 exit 失敗），
// 未注入時預設回 no-op 成功（exit_code = 0，空輸出）。**絕不 spawn / exec / fork。**
// ---------------------------------------------------------------------------
class NullCommandRunner : public CommandRunner {
public:
    NullCommandRunner() = default;

    // 注入固定結果：之後每次 run 都記錄請求並回此結果（供模擬成功 / 失敗）。
    explicit NullCommandRunner(RunResult injected)
        : injected_(std::move(injected)), has_injected_(true) {}

    RunResult run(const CommandSpec& spec) override {
        records_.push_back(spec);
        return has_injected_ ? injected_ : RunResult{};  // 預設 no-op 成功
    }

    // 設定 / 清除注入結果。
    void set_result(RunResult result) {
        injected_ = std::move(result);
        has_injected_ = true;
    }
    void clear_result() noexcept {
        injected_ = RunResult{};
        has_injected_ = false;
    }

    // 已記錄的請求（依發生序）。供測試 / 診斷內省。
    const std::vector<CommandSpec>& records() const noexcept { return records_; }
    std::size_t record_count() const noexcept { return records_.size(); }
    bool empty() const noexcept { return records_.empty(); }
    void clear() noexcept { records_.clear(); }

    // 最近一次記錄（無記錄回 nullptr）。
    const CommandSpec* last() const noexcept {
        return records_.empty() ? nullptr : &records_.back();
    }

private:
    RunResult injected_{};
    bool has_injected_ = false;
    std::vector<CommandSpec> records_;
};

// ---------------------------------------------------------------------------
// CommandActuator — 把 `command.run` 具名命令掛上 E6-01 命令匯流排的致動器。
//
// 建構時綁定一個 CommandRunner（相位 1 為 NullCommandRunner）。register_on(bus)
// 將 command.run 註冊到匯流排；呼叫端之後只需 bus.dispatch("command.run", args)
// 即可觸發，完全不需相依本型別。
//
// 命令參數契約（皆以 E6-01 CommandArgs 承載，必填參數以 has()/get_string 保護）：
//   - command.run：必填 `program`（字串、非空）；
//                  選用 `args`（字串，單一命令列參數，空白分隔為多個）；
//                  選用 `cwd`（字串，工作目錄）；
//                  選用 `env`（字串，形如 "K=V" 的單一環境變數）。
// 缺必填參數 / 空值 / 型別不符 → 回 CommandResult{Failed}（不崩潰、不丟例外、不觸及執行器）。
//
// 執行結果對映：執行器回 exit_code == 0 → CommandResult{Ok}（value = exit_code、
// message = stdout）；非零 → CommandResult{Failed}（value = exit_code、message = stderr）。
// 「執行本身有進行」與「命令回報失敗」清楚區分：後者仍是一次成功的分派 + 執行。
// ---------------------------------------------------------------------------
class CommandActuator {
public:
    explicit CommandActuator(std::shared_ptr<CommandRunner> runner)
        : runner_(std::move(runner)) {}

    // 便捷建構：預設綁 NullCommandRunner（相位 1）。
    CommandActuator() : runner_(std::make_shared<NullCommandRunner>()) {}

    // 綁定的執行器（可為 null 檢查用）。
    const std::shared_ptr<CommandRunner>& runner() const noexcept { return runner_; }

    // 將 command.run 具名命令註冊到匯流排。成功回 true；id 已被占用 / 無執行器則回 false。
    bool register_on(ds::command::CommandBus& bus) {
        if (!runner_) return false;
        auto self = this;
        return bus.register_command(
            kCmdCommandRun, [self](const ds::command::CommandArgs& a) {
                return self->handle_command_run(a);
            });
    }

    // 從匯流排移除 command.run。回傳是否確有移除。
    bool unregister_from(ds::command::CommandBus& bus) {
        return bus.unregister(kCmdCommandRun);
    }

    // ---- 處理器（亦可直接呼叫，方便測試不經匯流排也能驗證語意）----

    ds::command::CommandResult handle_command_run(const ds::command::CommandArgs& args) {
        if (!args.has("program")) {
            return ds::command::CommandResult::make_failed("command.run: missing 'program'");
        }
        const auto program = args.get_string("program");
        if (!program || program->empty()) {
            return ds::command::CommandResult::make_failed(
                "command.run: 'program' must be a non-empty string");
        }
        CommandSpec spec;
        spec.program = *program;
        if (args.has("args")) {
            const auto a = args.get_string("args");
            if (!a) {
                return ds::command::CommandResult::make_failed(
                    "command.run: 'args' must be a string");
            }
            spec.arguments = split_whitespace(*a);
        }
        if (args.has("cwd")) {
            const auto c = args.get_string("cwd");
            if (!c) {
                return ds::command::CommandResult::make_failed(
                    "command.run: 'cwd' must be a string");
            }
            spec.working_directory = *c;
        }
        if (args.has("env")) {
            const auto e = args.get_string("env");
            if (!e) {
                return ds::command::CommandResult::make_failed(
                    "command.run: 'env' must be a string");
            }
            EnvVar ev;
            if (parse_env(*e, ev)) spec.environment.push_back(ev);
        }
        return dispatch_to_runner(spec);
    }

    // 直接以組好的 CommandSpec 執行（跳過參數解析；供進階呼叫端 / 測試）。
    ds::command::CommandResult run_spec(const CommandSpec& spec) {
        if (spec.program.empty()) {
            return ds::command::CommandResult::make_failed(
                "command.run: 'program' must be a non-empty string");
        }
        return dispatch_to_runner(spec);
    }

private:
    ds::command::CommandResult dispatch_to_runner(const CommandSpec& spec) {
        if (!runner_) {
            return ds::command::CommandResult::make_failed("no runner bound");
        }
        const RunResult r = runner_->run(spec);
        if (r.success()) {
            return ds::command::CommandResult::make_ok(
                ds::command::CommandValue{static_cast<std::int64_t>(r.exit_code)},
                r.stdout_text);
        }
        return ds::command::CommandResult::make_failed(
            r.stderr_text.empty() ? ("command exited with code " + std::to_string(r.exit_code))
                                  : r.stderr_text,
            ds::command::CommandValue{static_cast<std::int64_t>(r.exit_code)});
    }

    // 以空白（空格 / tab）切分為多個參數；連續空白不產生空 token。純字串操作、無平台分支。
    static std::vector<std::string> split_whitespace(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        for (char ch : s) {
            if (ch == ' ' || ch == '\t') {
                if (!cur.empty()) {
                    out.push_back(cur);
                    cur.clear();
                }
            } else {
                cur.push_back(ch);
            }
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    // 解析 "K=V" 形式的環境變數；無 '=' 或鍵為空回 false（不加入）。
    static bool parse_env(const std::string& s, EnvVar& out) {
        const auto pos = s.find('=');
        if (pos == std::string::npos || pos == 0) return false;
        out.name = s.substr(0, pos);
        out.value = s.substr(pos + 1);
        return true;
    }

    std::shared_ptr<CommandRunner> runner_;
};

}  // namespace ds::actuators

#endif  // DS_ACTUATORS_E3_01_COMMAND_ACTUATOR_HPP
