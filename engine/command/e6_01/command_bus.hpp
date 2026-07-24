// E6-01 命令匯流排與分派 — 平台中立契約（擴充點 3「動作」）
//
// 平台的第三個擴充點是「動作」：讓任意致動器（音量、電源、任意副作用）以具名命令
// 掛上匯流排，呼叫端只需 命令 id + 參數 即可觸發，**不需相依任何具體致動器**。
// 致動器系統（modules/actuators）與更上層的動作系統都消費本契約，因此 API 面要
// 最小、穩定、可長期承重。
//
// 本單元屬 engine 層（平台中立純邏輯）：
//   - 不綁任何真實副作用 / OS 後端；「動作」由呼叫端註冊的處理器自行實現。
//   - 參數與回傳皆以穩定的值型別（CommandValue / CommandArgs）承載，跨模組邊界不變形。
//   - 未知命令回結構化錯誤（NotFound），**絕不崩潰**。
// 因此可完全以單元測試驗證：註冊 → 分派 → 驗證處理器被呼叫、參數傳遞、未知命令、
// 重複註冊、unregister。無 `#ifdef` / 平台分支 / 後端。
#ifndef DS_COMMAND_E6_01_COMMAND_BUS_HPP
#define DS_COMMAND_E6_01_COMMAND_BUS_HPP

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ds::command {

// 命令識別碼：具名字串。穩定、可讀、跨模組邊界不變形；不使用數字 opcode（避免耦合）。
using CommandId = std::string;

// 擴充點契約版本標記。此契約承重（致動器 + 動作系統消費），版本欄位讓消費者可
// 在演進時做相容性判斷（見 architecture.md「Q3 的意義加重」）。定義在 .cpp。
const char* contract_version() noexcept;

// ---------------------------------------------------------------------------
// CommandValue — 命令參數 / 回傳所用的穩定值型別。
//
// 刻意限定為一小組可長期承重的基本型別（null / bool / 整數 / 浮點 / 字串），
// 讓契約跨越致動器邊界時不需相依任何具體型別。需要複合結構時，呼叫端以多個具名
// 參數表達，而非在此塞入任意型別。
// ---------------------------------------------------------------------------
class CommandValue {
public:
    enum class Type { Null, Bool, Int, Double, String };

    CommandValue() noexcept : data_(std::monostate{}) {}
    CommandValue(bool b) noexcept : data_(b) {}
    CommandValue(std::int64_t i) noexcept : data_(i) {}
    CommandValue(int i) noexcept : data_(static_cast<std::int64_t>(i)) {}
    CommandValue(double d) noexcept : data_(d) {}
    CommandValue(std::string s) : data_(std::move(s)) {}
    CommandValue(const char* s) : data_(std::string(s)) {}

    Type type() const noexcept { return static_cast<Type>(data_.index()); }
    bool is_null() const noexcept { return type() == Type::Null; }

    // 型別安全存取：型別相符回值，否則回 std::nullopt（不丟例外、不 UB）。
    std::optional<bool> as_bool() const {
        if (auto* p = std::get_if<bool>(&data_)) return *p;
        return std::nullopt;
    }
    std::optional<std::int64_t> as_int() const {
        if (auto* p = std::get_if<std::int64_t>(&data_)) return *p;
        return std::nullopt;
    }
    std::optional<double> as_double() const {
        if (auto* p = std::get_if<double>(&data_)) return *p;
        return std::nullopt;
    }
    std::optional<std::string> as_string() const {
        if (auto* p = std::get_if<std::string>(&data_)) return *p;
        return std::nullopt;
    }

    // 值相等（型別 + 內容皆同才相等）。供測試與去重使用。
    bool operator==(const CommandValue& o) const noexcept { return data_ == o.data_; }
    bool operator!=(const CommandValue& o) const noexcept { return !(*this == o); }

private:
    std::variant<std::monostate, bool, std::int64_t, double, std::string> data_;
};

// ---------------------------------------------------------------------------
// CommandArgs — 具名參數字典（穩定值型別的鍵值集合）。
//
// 有序（std::map）以確保遍歷 / 測試的決定性。提供 has() 與型別安全的 getter。
// ---------------------------------------------------------------------------
class CommandArgs {
public:
    CommandArgs() = default;

    // 設定 / 覆寫一個具名參數；回傳 *this 以便鏈式呼叫。
    CommandArgs& set(std::string key, CommandValue value) {
        values_[std::move(key)] = std::move(value);
        return *this;
    }

    // 是否含指定鍵。
    bool has(const std::string& key) const { return values_.find(key) != values_.end(); }

    // 取原始值指標；不存在回 nullptr（不新增鍵）。
    const CommandValue* find(const std::string& key) const {
        auto it = values_.find(key);
        return it == values_.end() ? nullptr : &it->second;
    }

    // 型別安全 getter：鍵不存在或型別不符皆回 std::nullopt。
    std::optional<bool> get_bool(const std::string& key) const {
        if (auto* v = find(key)) return v->as_bool();
        return std::nullopt;
    }
    std::optional<std::int64_t> get_int(const std::string& key) const {
        if (auto* v = find(key)) return v->as_int();
        return std::nullopt;
    }
    std::optional<double> get_double(const std::string& key) const {
        if (auto* v = find(key)) return v->as_double();
        return std::nullopt;
    }
    std::optional<std::string> get_string(const std::string& key) const {
        if (auto* v = find(key)) return v->as_string();
        return std::nullopt;
    }

    std::size_t size() const noexcept { return values_.size(); }
    bool empty() const noexcept { return values_.empty(); }

private:
    std::map<std::string, CommandValue> values_;
};

// ---------------------------------------------------------------------------
// CommandStatus / CommandResult — 分派結果。
//
// NotFound 為匯流排自身在未知命令時產生（絕不崩潰）；Ok / Failed 由處理器決定。
// ---------------------------------------------------------------------------
enum class CommandStatus {
    Ok,        // 命令已分派、處理器回報成功
    NotFound,  // 匯流排無此命令（匯流排產生，非處理器）
    Failed,    // 命令已分派，但處理器回報失敗
};

struct CommandResult {
    CommandStatus status = CommandStatus::Ok;
    CommandValue value{};    // 選用回傳值（如查詢類命令）
    std::string message{};   // 人類可讀訊息，失敗時尤其有用

    bool ok() const noexcept { return status == CommandStatus::Ok; }

    // 工廠：讓處理器 / 匯流排以意圖清楚的方式建構結果。
    static CommandResult make_ok(CommandValue value = {}, std::string message = {}) {
        return CommandResult{CommandStatus::Ok, std::move(value), std::move(message)};
    }
    static CommandResult make_failed(std::string message, CommandValue value = {}) {
        return CommandResult{CommandStatus::Failed, std::move(value), std::move(message)};
    }
    static CommandResult make_not_found(const CommandId& id) {
        return CommandResult{CommandStatus::NotFound, CommandValue{}, "unknown command: " + id};
    }
};

// 命令處理器：接收參數、執行動作、回傳結果。由致動器 / 動作提供並註冊。
using CommandHandler = std::function<CommandResult(const CommandArgs&)>;

// 具名命令值型別：id + 參數的打包（擴充點契約中「命令」的具體形狀）。
// 呼叫端可先組好一個 Command 再交給匯流排分派。
struct Command {
    CommandId id;
    CommandArgs args;
};

// ---------------------------------------------------------------------------
// CommandBus — 命令匯流排：具名命令的註冊表與分派器。
//
// 致動器以 register_command(id, handler) 掛上；呼叫端以 dispatch(id, args) 觸發，
// 不需相依任何具體致動器。契約保證：
//   - 註冊：具名 id → 處理器。空 id / 空處理器 / 已存在的 id 一律拒絕（回 false），
//     **不靜默覆蓋**已註冊者（避免致動器被無聲遮蔽）。
//   - 分派未知命令：回 CommandResult{NotFound}，不崩潰、不丟例外。
//   - unregister：移除具名命令；回是否確有移除。
//   - has_command：存在性查詢。
// ---------------------------------------------------------------------------
class CommandBus {
public:
    CommandBus() = default;

    // 註冊具名命令。id 非空、handler 非空、且該 id 尚未註冊時成功並回 true；
    // 否則不變更狀態並回 false（重複註冊不覆蓋）。
    bool register_command(CommandId id, CommandHandler handler) {
        if (id.empty() || !handler) return false;
        if (handlers_.find(id) != handlers_.end()) return false;
        handlers_.emplace(std::move(id), std::move(handler));
        return true;
    }

    // 取消註冊具名命令。回傳是否確有移除（未知 id 回 false）。
    bool unregister(const CommandId& id) { return handlers_.erase(id) > 0; }

    // 是否已註冊指定命令。
    bool has_command(const CommandId& id) const {
        return handlers_.find(id) != handlers_.end();
    }

    // 分派具名命令。未知命令回 CommandResult{NotFound}（不崩潰）；
    // 否則呼叫其處理器並回傳處理器結果。
    CommandResult dispatch(const CommandId& id, const CommandArgs& args) const {
        auto it = handlers_.find(id);
        if (it == handlers_.end()) return CommandResult::make_not_found(id);
        return it->second(args);
    }

    // 便捷多載：無參數分派。
    CommandResult dispatch(const CommandId& id) const { return dispatch(id, CommandArgs{}); }

    // 便捷多載：以打包的 Command 值型別分派。
    CommandResult dispatch(const Command& command) const {
        return dispatch(command.id, command.args);
    }

    // 現有已註冊命令數。
    std::size_t command_count() const noexcept { return handlers_.size(); }

    // 列舉所有已註冊命令 id（有序）。供內省 / 診斷。
    std::vector<CommandId> command_ids() const {
        std::vector<CommandId> ids;
        ids.reserve(handlers_.size());
        for (const auto& kv : handlers_) ids.push_back(kv.first);
        return ids;  // std::map 已排序
    }

private:
    std::map<CommandId, CommandHandler> handlers_;
};

}  // namespace ds::command

#endif  // DS_COMMAND_E6_01_COMMAND_BUS_HPP
