// E10-05 獨立行程 widget 宿主 — 平台中立契約（可注入 process host 抽象，相位 1 不真的 fork）
//
// 語意：在（模擬的）獨立行程中託管 widget 的宿主抽象——讓 widget 可跑在與主程式隔離的
// 行程（真實隔離的目的是崩潰隔離 / 資源限制），宿主管理 widget 的生命週期（啟動 / 停止 /
// 監看 / 重啟）、透過 E10-01 `MessageChannel` 與主程式通訊、widget 的 surface 經 E1-03
// `AlphaSurfaceService` 協定橋接（啟動時建立對應具名 surface 並廣播 attach 事件，停止時銷毀
// 並廣播 detach 事件）。
//
// 相位 1 刻意**不真的 fork / exec**：以**可注入的 `ProcessLauncher` 抽象**
// （spawn / terminate / is_alive）+ `NullProcessLauncher`（記憶體模擬）表達行程生命週期。
// 真實行程隔離（fork/exec、win32 CreateProcess...）留待相位 2 實作同介面的後端，`WidgetHost`
// 本身不需改動。
//
// 本單元屬 engine 層（平台中立純邏輯）：
//   - 無 `#ifdef` / `_WIN32` / `cocoa` 等平台分支，不綁任何真實行程 / OS API。
//   - 崩潰與無效 spec 一律經明確狀態碼 / 回呼回報，**絕不靜默吞掉**。
//   - widget / 行程一律以**具名識別碼**指涉（NFR-02：不用數字 handle / index）。
#ifndef DS_IPC_E10_05_WIDGET_HOST_HPP
#define DS_IPC_E10_05_WIDGET_HOST_HPP

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>

#include "alpha_surface.hpp"    // E1-03（上游，可讀不可改）：widget surface 協定橋接
#include "message_channel.hpp"  // E10-01（上游，可讀不可改）：與主程式通訊的通道

namespace ds::ipc {

// 契約版本標記，供消費者於相位 2 換上真實行程隔離時做相容性判斷（與 E10-01 同作法）。
// 以 widget_host_ 前綴避免與 E10-01 的 contract_version()（同 ds::ipc 命名空間）撞名——
// 本單元 PUBLIC 相依 e10_01，兩者符號會出現在同一連結單元。
const char* widget_host_contract_version() noexcept;

// widget 的穩定具名識別碼（如 "widget.clock"）；不使用數字 id（NFR-02 一致慣例）。
using WidgetId = std::string;

// 行程的穩定具名代號；由 `ProcessLauncher` 實作決定其形狀，呼叫端視為不透明權柄
// （不得假設其內部結構、不得當數字 index 使用）。空字串保留為「無效 / 啟動失敗」。
using ProcessHandle = std::string;

// ---------------------------------------------------------------------------
// WidgetSpec — 描述如何啟動一個 widget 行程。
//
// `id` / `entry` 為必填（空值視為無效 spec）。`surface_id` 選填——非空時，
// `WidgetHost::start()` 會經注入的 `AlphaSurfaceService` 為該 widget 橋接一個
// 具名 alpha surface；空字串表示此 widget 不需要（或不橋接）surface。
// ---------------------------------------------------------------------------
struct WidgetSpec {
    WidgetId id;                                // 具名 widget 識別碼；空 = 無效 spec
    std::string entry;                          // 具名進入點（如 "widget.clock.main"）；空 = 無效 spec
    ds::kernel::SurfaceId surface_id;           // 該 widget 對應的具名 surface；空 = 不橋接 surface
    ds::kernel::AlphaProfile surface_alpha{};   // 橋接 surface 時採用的初始 alpha profile
    ds::command::CommandArgs launch_args;       // 啟動參數（重用 E6-01 穩定值型別，隨協定攜帶）
};

// ---------------------------------------------------------------------------
// ProcessLauncher — 可注入的行程宿主抽象（相位 1：不真的 fork/exec）。
//
// `WidgetHost` 透過此抽象管理 widget 行程的啟動 / 終止 / 存活查詢；真實行程隔離
// （fork/exec、win32 CreateProcess...）留待相位 2 實作同介面的後端即可、`WidgetHost`
// 本身不需改動。
// ---------------------------------------------------------------------------
class ProcessLauncher {
public:
    virtual ~ProcessLauncher() = default;

    // 啟動一個行程來跑指定 widget spec。成功回一個非空 `ProcessHandle`；
    // 無法啟動（含 spec 於本層被判定無效）回空字串（明確失敗，不丟例外）。
    virtual ProcessHandle spawn(const WidgetSpec& spec) = 0;
    // 終止指定行程；回傳是否確有終止（未知 / 已終止的 handle 回 false，不崩潰）。
    virtual bool terminate(const ProcessHandle& handle) = 0;
    // 該行程目前是否存活；未知 handle 回 false（保守）。
    virtual bool is_alive(const ProcessHandle& handle) const = 0;
};

// ---------------------------------------------------------------------------
// NullProcessLauncher — 相位 1 參考實作：記憶體模擬行程表，不含任何真實 OS 呼叫。
//
// 以遞增具名 handle（"proc.<widget_id>#<n>"）模擬行程；額外提供 `simulate_crash()`
// 供測試 / 上層注入「非預期終止」情境（與 `terminate()` 的差異純粹是語意上「誰主動」，
// 兩者對 `is_alive()` 的效果相同——區分崩潰 vs. 正常停止是 `WidgetHost` 的職責）。
// ---------------------------------------------------------------------------
class NullProcessLauncher : public ProcessLauncher {
public:
    ProcessHandle spawn(const WidgetSpec& spec) override;
    bool terminate(const ProcessHandle& handle) override;
    bool is_alive(const ProcessHandle& handle) const override;

    // 測試 / 模擬用：使指定行程進入「非預期終止」狀態。未知 handle 或已死回 false。
    bool simulate_crash(const ProcessHandle& handle);

    // 目前（含已終止 / 已崩潰的歷史記錄）已知的行程數。供測試斷言「無孤兒行程」等。
    std::size_t process_count() const noexcept { return processes_.size(); }

private:
    struct Entry {
        bool alive = false;
    };
    std::map<ProcessHandle, Entry> processes_;
    std::uint64_t next_id_ = 1;
};

// widget 行程目前的存續狀態。
enum class ProcessState {
    NotStarted,  // 尚未啟動過
    Running,     // 執行中
    Stopped,     // 正常終止（呼叫 stop()）
    Crashed,     // 非預期終止（poll_crash() 偵測到）
};

// `start()` / `stop()` / `restart()` 的結果碼——崩潰 / 無效輸入一律明確回報，不靜默。
enum class WidgetHostStatus {
    Ok,                  // 操作成功
    Invalid,             // 前置條件不滿足（空 id/entry、spec 被 launcher 拒絕、surface id 衝突等）
    AlreadyRunning,      // start()：目前已有 widget 在跑
    NotRunning,          // stop()：目前並非執行中
    SurfaceUnsupported,  // 要求 surface 橋接，但 AlphaSurfaceService 回報能力不可用（NFR-03）
};

// widget 宿主事件的訊息型別（經 E10-01 通道廣播；供主程式訂閱觀察）。
inline constexpr const char* kWidgetStartedType = "widget.host.started";
inline constexpr const char* kWidgetStoppedType = "widget.host.stopped";
inline constexpr const char* kWidgetCrashedType = "widget.host.crashed";
inline constexpr const char* kWidgetSurfaceAttachedType = "widget.host.surface_attached";
inline constexpr const char* kWidgetSurfaceDetachedType = "widget.host.surface_detached";

// ---------------------------------------------------------------------------
// WidgetHost — 在（模擬的）獨立行程中託管單一 widget 的宿主。
//
// 職責：
//   - 生命週期：`start` / `stop` / `restart`，委由注入的 `ProcessLauncher`。
//   - 崩潰偵測：`poll_crash()`（相位 1 無真實訊號，呼叫端輪詢；亦可經
//     `NullProcessLauncher::simulate_crash()` 觸發），偵測到即轉 `Crashed` 並呼叫
//     `on_crash` 回呼——明確通知，不靜默吞掉。
//   - 通訊：經 E10-01 `MessageChannel` 廣播宿主事件（started/stopped/crashed/
//     surface_attached/surface_detached），並透過 `channel()` 供呼叫端直接收發。
//   - Surface 橋接：`spec.surface_id` 非空時，`start()` 經注入的
//     `AlphaSurfaceService` 建立該 widget 的具名 alpha surface 並廣播 attach 事件；
//     `stop()` / 重啟前銷毀並廣播 detach 事件。
//
// 一個 `WidgetHost` 實例對應一個 widget 行程（同時最多一個在跑）。
// ---------------------------------------------------------------------------
class WidgetHost {
public:
    // 綁定注入的 launcher / channel（不取得所有權，須存活於本物件之外）；
    // `surface_service` 選填——若某 widget spec 要求 surface 橋接但未提供，`start()`
    // 明確回報 `Invalid`（不靜默略過橋接）。
    WidgetHost(ProcessLauncher& launcher, MessageChannel& channel,
               ds::kernel::AlphaSurfaceService* surface_service = nullptr)
        : launcher_(launcher), channel_(channel), surface_service_(surface_service) {}

    // 啟動指定 spec 的 widget。
    //   - `spec.id` / `spec.entry` 空 → `Invalid`，不啟動任何行程。
    //   - 目前已在跑 → `AlreadyRunning`（不重複啟動、不覆蓋目前行程）。
    //   - `spec.surface_id` 非空但未注入 `surface_service` → `Invalid`。
    //   - `spec.surface_id` 非空但能力不可用 → `SurfaceUnsupported`（不啟動行程）。
    //   - `spec.surface_id` 已是既有 alpha surface → `Invalid`（id 衝突，不啟動行程）。
    //   - launcher 拒絕（回空 handle） → `Invalid`。
    //   - 行程啟動後 surface 建立失敗 → 回滾（終止剛啟動的行程，不留孤兒），回對應狀態碼。
    //   - 成功 → `Ok`：狀態轉 `Running`，廣播 `kWidgetStartedType`
    //     （與 `kWidgetSurfaceAttachedType`，若有橋接 surface）。
    WidgetHostStatus start(WidgetSpec spec);

    // 停止目前 widget（視為正常終止，非崩潰）；銷毀已橋接的 surface（若有）。
    // 非執行中 → `NotRunning`（不崩潰）。成功 → `Ok`，廣播 `kWidgetStoppedType`
    // （與 `kWidgetSurfaceDetachedType`，若曾橋接 surface）。
    WidgetHostStatus stop();

    // 重啟：以上次 `start()` 的 spec 停止（若在跑或已崩潰）後重新啟動。
    // 未曾啟動過（無 spec 可重啟）→ `Invalid`。其餘語意同 `start()`。
    WidgetHostStatus restart();

    // 目前狀態（本地記錄，經 `start`/`stop`/`poll_crash` 更新）。
    ProcessState state() const noexcept { return state_; }

    // 即時查詢：狀態為 `Running` 且注入的 launcher 回報該行程仍存活。
    // 與 `state()` 的差異：`state()` 是最近一次已知狀態，`is_alive()` 即時查詢 launcher。
    bool is_alive() const;

    // 崩潰偵測：僅在目前狀態為 `Running` 時有意義。若 launcher 回報行程已不存活，
    // 轉為 `Crashed`、廣播 `kWidgetCrashedType`、呼叫 `on_crash` 回呼（若已設定），
    // 並回傳 true（偵測到新崩潰）；否則回傳 false（非執行中，或仍存活）。
    bool poll_crash();

    // 設定崩潰回呼；偵測到崩潰時以目前 `WidgetId` 呼叫一次。
    void on_crash(std::function<void(const WidgetId&)> cb) { on_crash_ = std::move(cb); }

    // 經 E10-01 通道與主程式通訊的存取點（發布訂閱 + 點對點佇列，見 E10-01 契約）。
    MessageChannel& channel() const noexcept { return channel_; }

    // 是否曾成功啟動過（`restart()` 是否可用的前置條件）。
    bool has_widget() const noexcept { return has_spec_; }
    // 目前 / 最近一次的 widget 識別碼；未曾啟動過回空字串。
    const WidgetId& widget_id() const noexcept { return spec_.id; }
    // 目前 / 最近一次的行程 handle；未曾啟動過回空字串。
    const ProcessHandle& process_handle() const noexcept { return handle_; }

private:
    void publish_lifecycle(const MessageType& type) const;
    void publish_surface_event(const MessageType& type) const;
    // 停止目前行程與（若有）已橋接的 surface；不變更 `state_`（由呼叫端決定轉往哪個狀態）。
    void teardown_running();

    ProcessLauncher& launcher_;
    MessageChannel& channel_;
    ds::kernel::AlphaSurfaceService* surface_service_;

    WidgetSpec spec_;
    ProcessHandle handle_;
    ProcessState state_ = ProcessState::NotStarted;
    bool has_spec_ = false;          // 是否曾成功 start() 過（restart() 前置條件）
    bool surface_attached_ = false;  // 目前是否有經本宿主橋接、尚未銷毀的 surface

    std::function<void(const WidgetId&)> on_crash_;
};

}  // namespace ds::ipc

#endif  // DS_IPC_E10_05_WIDGET_HOST_HPP
