// E8-03 表現控制指令集 — 一組控制表現 / 演出的高階具名指令，供 E8-02 對話腳本編排視覺
// （engine 層 / 描述子系統，平台中立）
//
// 語意：腳本（E8-02）要能編排「畫面上發生什麼」——切換目前顯示的 surface、show / hide 具名
// 元件、播一段轉場、等待——但腳本直譯器本身（E8-02 `Interpreter`）**不**認識這些字彙：它只有
// `set` / `say` / `if` / `goto` / `label` 五個 opcode，未知 opcode 會被它自己判定為未知指令而
// 報錯（見 `interpreter.hpp` 檔首）。因此本單元**不修改、不擴充 E8-02 的 opcode**，而是在其之上
// 疊一層：
//
//   1. **具名指令表**：`PresentationController` 把 `show` / `hide` / `switch_surface` /
//      `transition` / `wait` 五個具名動作註冊到 E6-01 `CommandBus`——呼叫端（腳本、測試、
//      未來的動作系統）一律以「命令 id + 具名參數」分派，不需相依這裡的具體型別。
//      `switch_surface` 的實際切換委派給 E4-06 `SurfaceSwitcher`；其餘四個為 phase 1 的純
//      記憶體描述 / 分派，不做真實繪製（NFR-02 精神：全程具名字串，無座標 / 無數字 z-order）。
//   2. **與 E8-02 整合**：`PresentationSink` 實作 E8-02 的 `OutputSink`。腳本以既有的
//      `say <text>` 步驟 emit 一行「表現指令文字」（如 `"show hero"` / `"switch_surface
//      main"` / `"transition fade a b 0.5"` / `"wait 1.0"`），本 sink 把該行解析為具名命令
//      並經 `CommandBus::dispatch` 分派——腳本完全不需要知道背後是 E4-06 / CommandBus。
//
// 不靜默失敗（NFR-04 精神）：
//   - 未知指令（不在 show/hide/switch_surface/transition/wait 之列）→ 交給 CommandBus
//     分派一個未註冊的 id，CommandBus 依其自身契約回 `CommandStatus::NotFound`（見
//     `command_bus.hpp`）——本單元不吞、不忽略，`PresentationSink::results()` 忠實記錄每一行
//     的分派結果供呼叫端 / 測試逐一檢查。
//   - 無效參數（缺參數、空字串目標、非有限 / 負值秒數、未知 surface）→ 各處理器回
//     `CommandStatus::Failed` 並帶人類可讀訊息，**不套用、不部分套用**。
//
// 相位 1：純描述 / 分派，不做真實繪製；無 `#ifdef` / win32 / cocoa / 任何真實後端。
// 命名空間 `ds::script`（延續 E8-02，本單元亦屬對話腳本子系統的表現層擴充）。
#ifndef DS_ENGINE_E8_03_PRESENTATION_COMMANDS_HPP
#define DS_ENGINE_E8_03_PRESENTATION_COMMANDS_HPP

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "command_bus.hpp"       // E6-01（上游，可讀不可改）：CommandBus / CommandArgs / CommandResult
#include "surface_switcher.hpp"  // E4-06（上游，可讀不可改）：SurfaceSwitcher / SwitchStatus
#include "interpreter.hpp"       // E8-02（上游，可讀不可改）：OutputSink（整合點）

namespace ds::script {

// 沿用上游型別（不重造）。
using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandId;
using ds::command::CommandResult;
using ds::command::CommandStatus;
using ds::render::SurfaceSwitcher;
using ds::render::SwitchStatus;

// -----------------------------------------------------------------------------
// 具名指令表 —— 命令 id 一律具名字串，掛在 `ds::script.` 命名前綴下（避免與其他子系統的
// 命令在同一個 CommandBus 上撞名；NFR-02：具名，非數字 opcode）。
// -----------------------------------------------------------------------------
namespace presentation_commands {

inline constexpr const char* kShow = "ds::script.show";                    // show <target>
inline constexpr const char* kHide = "ds::script.hide";                    // hide <target>
inline constexpr const char* kSwitchSurface = "ds::script.switch_surface"; // switch_surface <target>
inline constexpr const char* kTransition = "ds::script.transition";       // transition <kind> <from> <to> <duration>
inline constexpr const char* kWait = "ds::script.wait";                   // wait <seconds>

}  // namespace presentation_commands

// -----------------------------------------------------------------------------
// 轉場 / 等待紀錄 —— phase 1 純描述（不做真實動畫 / 真實計時），供呼叫端 / 測試查驗。
// -----------------------------------------------------------------------------

// 一次 `transition` 指令的具名描述：`kind` 為具名轉場種類（如 "fade" / "cut" / "slide"，
// 非數字係數），`from` / `to` 為具名端點（元件或 surface id，本單元不強制其存在性——
// 轉場是「演出描述」，与 switch_surface 是否真的切換 surface 是分開的動作）。
struct TransitionRecord {
    std::string kind;
    std::string from;
    std::string to;
    double duration_seconds = 0.0;  // 秒；必須為有限值且 >= 0。
};

// 一次 `wait` 指令的描述：phase 1 不真的睡眠，只記錄「腳本要求等待多久」。
struct WaitRecord {
    double seconds = 0.0;  // 必須為有限值且 >= 0。
};

// -----------------------------------------------------------------------------
// PresentationController —— 具名表現指令表的宿主：註冊 show/hide/switch_surface/
// transition/wait 到注入的 CommandBus，並持有 phase 1 的純記憶體演出狀態。
//
// 不取得 `surfaces` / `bus` 的所有權；兩者須於本物件存活期間有效。建構時嘗試把五個指令
// 註冊到 `bus`；若因 id 已被佔用（如同一 bus 上有第二個 controller）而註冊失敗，
// `ready()` 回 false 且 `registration_error()` 給出人類可讀原因（不靜默、不部分套用）——
// 呼叫端應於使用前檢查 `ready()`。解構時會 unregister 本物件成功註冊過的所有 id，
// 使 bus 可安全地被其後續使用者重新註冊（測試常見模式：同一 bus、多個 controller 生命週期）。
// -----------------------------------------------------------------------------
class PresentationController {
public:
    PresentationController(SurfaceSwitcher& surfaces, CommandBus& bus);
    ~PresentationController();

    PresentationController(const PresentationController&) = delete;
    PresentationController& operator=(const PresentationController&) = delete;

    // 五個指令是否皆成功掛上 bus（見類別註解：僅在 id 衝突時可能為 false）。
    bool ready() const noexcept { return registration_ok_; }
    const std::string& registration_error() const noexcept { return registration_error_; }

    // --- 查詢 phase 1 演出狀態（供呼叫端 / 測試查驗；純記憶體，不代表真實繪製結果）---
    // 具名元件目前是否處於「顯示」狀態。從未 show 過、或最近一次是 hide → false（明確，
    // 不回傳未定義值）。
    bool is_visible(const std::string& target) const;
    std::size_t visible_count() const;

    const std::vector<TransitionRecord>& transitions() const noexcept { return transitions_; }
    const std::vector<WaitRecord>& waits() const noexcept { return waits_; }

    const SurfaceSwitcher& surfaces() const noexcept { return surfaces_; }

private:
    SurfaceSwitcher& surfaces_;
    CommandBus& bus_;

    bool registration_ok_ = false;
    std::string registration_error_;
    std::vector<CommandId> registered_ids_;  // 供解構時 unregister（僅含成功註冊者）。

    std::map<std::string, bool> visibility_;
    std::vector<TransitionRecord> transitions_;
    std::vector<WaitRecord> waits_;

    bool register_one(const CommandId& id, ds::command::CommandHandler handler);

    CommandResult handle_show(const CommandArgs& args);
    CommandResult handle_hide(const CommandArgs& args);
    CommandResult handle_switch_surface(const CommandArgs& args);
    CommandResult handle_transition(const CommandArgs& args);
    CommandResult handle_wait(const CommandArgs& args);
};

// -----------------------------------------------------------------------------
// PresentationSink —— E8-02 整合點：把 `say` 產生的一行文字解析為具名表現指令，經注入的
// CommandBus 分派（不直接持有 / 認識 PresentationController，只認識 CommandBus 這個穩定
// 擴充點契約——與 command_bus.hpp 檔首「呼叫端只需 命令 id + 參數」的精神一致）。
//
// 文字格式（以空白分隔的 token；第一個 token 為具名指令名稱）：
//     show <target>
//     hide <target>
//     switch_surface <target>
//     transition <kind> <from> <to> <duration>
//     wait <seconds>
// 指令名稱之外的第一個 token 一律視為未知指令：組出 `"ds::script." + token` 這個未註冊的
// id 交給 bus 分派，bus 依契約回 `NotFound`（見 command_bus.hpp）——不需額外的「未知指令」
// 特判邏輯，直接沿用 CommandBus 既有的不靜默契約。數值型參數（`duration` / `seconds`）
// 若整段 token 可解析為 `double` 則以數值型別帶入；否則原樣以字串帶入——處理器的數值型別
// 檢查（非數值 → 缺參數同等視為 Invalid）會據此拒絕，同樣不需要在此處特判。
// -----------------------------------------------------------------------------
class PresentationSink : public OutputSink {
public:
    // bus 不取得所有權；須於本物件存活期間有效。
    explicit PresentationSink(CommandBus& bus) : bus_(bus) {}

    // 解析 text 為具名命令並經 bus_ 分派；結果（含失敗）依序存入 results()，不吞任何錯誤。
    void emit(const std::string& text) override;

    const std::vector<CommandResult>& results() const noexcept { return results_; }
    // 是否**每一行**分派結果皆為 Ok（空歷史視為 true）。
    bool all_ok() const noexcept;
    std::size_t failure_count() const noexcept;
    std::size_t count() const noexcept { return results_.size(); }
    void clear() noexcept { results_.clear(); }

private:
    CommandBus& bus_;
    std::vector<CommandResult> results_;
};

}  // namespace ds::script

#endif  // DS_ENGINE_E8_03_PRESENTATION_COMMANDS_HPP
