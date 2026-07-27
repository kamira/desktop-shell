// E3-02 啟動程式 / 開檔 / 網頁搜尋致動器 — 平台中立契約（相位 1：介面 + null 後端）
//
// 語意：把「啟動外部程式」「用預設程式開啟檔案」「以搜尋引擎搜尋網頁」這三種副作用，
// 以具名命令掛上 E6-01 命令匯流排（`launch.program` / `open.file` / `web.search`）。
// 呼叫端只需 命令 id + 具名參數 即可觸發，不需相依本致動器或任何 OS API。
//
// 分層 / 相位：本單元屬 modules/actuators（動作層），消費 E6-01 契約。
//   - 相位 1（Mac / null 期）：**不真的啟動 / 開檔 / 搜尋**。所有請求交由可抽換的
//     `LaunchBackend` 承接；預設 `NullLaunchBackend` 只把請求「記錄」下來供測試 / 診斷驗證，
//     絕不觸碰 OS。相位 2 換上真實後端（win32 / cocoa）時，本致動器與命令契約一行不動。
//   - 無 `#ifdef` / 平台分支 / 真實 exec / spawn；唯一 `#ifndef` 為 header guard。
//
// 因此可完全以單元測試驗證：命令註冊到匯流排、dispatch 觸發後端、具名參數傳遞、
// null 後端記錄請求、必填參數缺漏回結構化失敗（不崩潰）。
#ifndef DS_ACTUATORS_E3_02_LAUNCH_ACTUATOR_HPP
#define DS_ACTUATORS_E3_02_LAUNCH_ACTUATOR_HPP

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "command_bus.hpp"  // E6-01：重用命令匯流排 / 穩定值型別 / 具名命令（PUBLIC 相依）

namespace ds::actuators {

// 擴充點契約版本標記（承重：呼叫端 / 相位 2 後端消費）。定義在 .cpp。
const char* contract_version() noexcept;

// 三個具名命令 id（穩定、可讀字串，不使用數字 opcode；與 E6-01 CommandId 取捨一致）。
inline constexpr const char* kCmdLaunchProgram = "launch.program";
inline constexpr const char* kCmdOpenFile      = "open.file";
inline constexpr const char* kCmdWebSearch     = "web.search";

// ---------------------------------------------------------------------------
// LaunchKind / LaunchRequest — 平台中立地描述一次「啟動」意圖。
//
// 不含任何 OS handle / 路徑語意假設；只承載呼叫端表達的目標與參數，讓相位 2 的真實
// 後端（或相位 1 的 null 後端）自行決定如何實現。
// ---------------------------------------------------------------------------
enum class LaunchKind {
    Program,    // 啟動外部程式（target = 程式名 / 路徑，arguments = 命令列參數）
    File,       // 以預設程式開啟檔案（target = 檔案路徑）
    WebSearch,  // 以搜尋引擎搜尋（target = 查詢字串，engine = 選用搜尋引擎鍵）
};

struct LaunchRequest {
    LaunchKind kind = LaunchKind::Program;
    std::string target;                    // 程式 / 檔案 / 查詢字串（依 kind）
    std::vector<std::string> arguments;    // 僅 Program 使用；其餘為空
    std::string engine;                    // 僅 WebSearch 使用；空表示由後端決定預設

    bool operator==(const LaunchRequest& o) const {
        return kind == o.kind && target == o.target &&
               arguments == o.arguments && engine == o.engine;
    }
    bool operator!=(const LaunchRequest& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// LaunchBackend — 執行實際副作用的抽象後端。
//
// 相位 1 僅提供 NullLaunchBackend；相位 2 由平台後端實作 perform() 真的啟動 / 開檔 / 搜尋。
// perform() 回 E6-01 CommandResult，讓匯流排呼叫端拿到一致的成功 / 失敗結構。
// ---------------------------------------------------------------------------
class LaunchBackend {
public:
    virtual ~LaunchBackend() = default;
    virtual ds::command::CommandResult perform(const LaunchRequest& request) = 0;
};

// ---------------------------------------------------------------------------
// NullLaunchBackend — 相位 1 預設後端：不觸碰 OS，只「記錄」每個請求。
//
// 讓致動器在無真實平台後端時仍可完整跑通（註冊 → 分派 → 記錄），並讓測試 / 診斷
// 驗證「呼叫端到底請求了什麼」。每次 perform 記錄請求並回 Ok。
// ---------------------------------------------------------------------------
class NullLaunchBackend : public LaunchBackend {
public:
    ds::command::CommandResult perform(const LaunchRequest& request) override {
        records_.push_back(request);
        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{request.target},
            "recorded (null backend, no real launch)");
    }

    // 已記錄的請求（依發生序）。供測試 / 診斷內省。
    const std::vector<LaunchRequest>& records() const noexcept { return records_; }
    std::size_t record_count() const noexcept { return records_.size(); }
    bool empty() const noexcept { return records_.empty(); }
    void clear() noexcept { records_.clear(); }

    // 最近一次記錄（無記錄回 nullptr）。
    const LaunchRequest* last() const noexcept {
        return records_.empty() ? nullptr : &records_.back();
    }

private:
    std::vector<LaunchRequest> records_;
};

// ---------------------------------------------------------------------------
// LaunchActuator — 把三個具名命令掛上 E6-01 命令匯流排的致動器。
//
// 建構時綁定一個 LaunchBackend（相位 1 為 NullLaunchBackend）。register_on(bus)
// 將 launch.program / open.file / web.search 註冊到匯流排；呼叫端之後只需
// bus.dispatch("launch.program", args) 即可觸發，完全不需相依本型別。
//
// 命令參數契約（皆以 E6-01 CommandArgs 承載，必填參數以 has()/get_string 保護）：
//   - launch.program：必填 `program`（字串）；選用 `args`（字串，單一命令列參數）。
//   - open.file     ：必填 `path`（字串）。
//   - web.search    ：必填 `query`（字串）；選用 `engine`（字串，搜尋引擎鍵）。
// 缺必填參數 → 回 CommandResult{Failed}（不崩潰、不丟例外）。
// ---------------------------------------------------------------------------
class LaunchActuator {
public:
    explicit LaunchActuator(std::shared_ptr<LaunchBackend> backend)
        : backend_(std::move(backend)) {}

    // 便捷建構：預設綁 NullLaunchBackend（相位 1）。
    LaunchActuator() : backend_(std::make_shared<NullLaunchBackend>()) {}

    // 綁定的後端（可為 null 檢查用）。
    const std::shared_ptr<LaunchBackend>& backend() const noexcept { return backend_; }

    // 將三個具名命令註冊到匯流排。全部成功才回 true；任一失敗（如 id 已被占用）
    // 則回滾已註冊者並回 false（不留半掛狀態）。無後端一律回 false。
    bool register_on(ds::command::CommandBus& bus) {
        if (!backend_) return false;
        auto self = this;
        const bool ok_prog = bus.register_command(
            kCmdLaunchProgram, [self](const ds::command::CommandArgs& a) {
                return self->handle_launch_program(a);
            });
        const bool ok_file = bus.register_command(
            kCmdOpenFile, [self](const ds::command::CommandArgs& a) {
                return self->handle_open_file(a);
            });
        const bool ok_search = bus.register_command(
            kCmdWebSearch, [self](const ds::command::CommandArgs& a) {
                return self->handle_web_search(a);
            });
        if (ok_prog && ok_file && ok_search) return true;
        // 回滾：只移除本次成功掛上的，避免遮蔽既有其他致動器。
        if (ok_prog) bus.unregister(kCmdLaunchProgram);
        if (ok_file) bus.unregister(kCmdOpenFile);
        if (ok_search) bus.unregister(kCmdWebSearch);
        return false;
    }

    // 從匯流排移除三個具名命令。回傳確有移除的數量（0..3）。
    std::size_t unregister_from(ds::command::CommandBus& bus) {
        std::size_t n = 0;
        n += bus.unregister(kCmdLaunchProgram) ? 1 : 0;
        n += bus.unregister(kCmdOpenFile) ? 1 : 0;
        n += bus.unregister(kCmdWebSearch) ? 1 : 0;
        return n;
    }

    // ---- 處理器（亦可直接呼叫，方便測試不經匯流排也能驗證語意）----

    ds::command::CommandResult handle_launch_program(const ds::command::CommandArgs& args) {
        if (!args.has("program")) {
            return ds::command::CommandResult::make_failed("launch.program: missing 'program'");
        }
        const auto program = args.get_string("program");
        if (!program || program->empty()) {
            return ds::command::CommandResult::make_failed(
                "launch.program: 'program' must be a non-empty string");
        }
        LaunchRequest req;
        req.kind = LaunchKind::Program;
        req.target = *program;
        if (args.has("args")) {
            if (const auto a = args.get_string("args")) req.arguments.push_back(*a);
        }
        return dispatch_to_backend(req);
    }

    ds::command::CommandResult handle_open_file(const ds::command::CommandArgs& args) {
        if (!args.has("path")) {
            return ds::command::CommandResult::make_failed("open.file: missing 'path'");
        }
        const auto path = args.get_string("path");
        if (!path || path->empty()) {
            return ds::command::CommandResult::make_failed(
                "open.file: 'path' must be a non-empty string");
        }
        LaunchRequest req;
        req.kind = LaunchKind::File;
        req.target = *path;
        return dispatch_to_backend(req);
    }

    ds::command::CommandResult handle_web_search(const ds::command::CommandArgs& args) {
        if (!args.has("query")) {
            return ds::command::CommandResult::make_failed("web.search: missing 'query'");
        }
        const auto query = args.get_string("query");
        if (!query || query->empty()) {
            return ds::command::CommandResult::make_failed(
                "web.search: 'query' must be a non-empty string");
        }
        LaunchRequest req;
        req.kind = LaunchKind::WebSearch;
        req.target = *query;
        if (args.has("engine")) {
            if (const auto e = args.get_string("engine")) req.engine = *e;
        }
        return dispatch_to_backend(req);
    }

private:
    ds::command::CommandResult dispatch_to_backend(const LaunchRequest& req) {
        if (!backend_) {
            return ds::command::CommandResult::make_failed("no backend bound");
        }
        return backend_->perform(req);
    }

    std::shared_ptr<LaunchBackend> backend_;
};

}  // namespace ds::actuators

#endif  // DS_ACTUATORS_E3_02_LAUNCH_ACTUATOR_HPP
