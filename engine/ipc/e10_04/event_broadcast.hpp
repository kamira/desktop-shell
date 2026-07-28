// E10-04 事件廣播型模組協定 — 平台中立契約（擴充點「模組間互通」的事件匯流排）。
//
// 載入的模組（E8-04）之間需要一種「不直接相依彼此」的互通方式：模組宣告它要
// 發布 / 訂閱哪些事件主題（topic），協定層把某主題上的事件廣播給所有訂閱該主題
// 的模組。發布者不需知道有誰在聽、聽者不需知道是誰發的 —— 徹底解耦。
//
// **相位 1：純記憶體事件匯流排，不綁真實網路 / IPC。**
//   實際的「依主題路由 + 依註冊序扇出 + 投遞計數」委派給 E10-01 的
//   `ds::ipc::MessageChannel`（本機 IPC 通道）—— 主題即通道的訊息型別（MessageType）。
//   E10-04 在其上再加「模組維度」：
//     - 每筆訂閱歸屬於某模組（ModuleId，對接 E8-04 manifest.name）；
//     - 模組卸載（E8-04 unload）時可一次註銷該模組的全部訂閱與主題宣告；
//     - 模組可宣告其 publish / subscribe 的主題，供內省 / 診斷。
//   等真實傳輸層（相位 2）到位時，本協定即為其上的事件層：底層通道換掉、
//   事件語意（依主題廣播、多訂閱者、無訂閱者不崩潰、模組卸載清理）不動。
//
// 本單元屬 engine 層（平台中立純邏輯）：
//   - 無 `#ifdef` / `_WIN32` / `cocoa` / `__APPLE__` 等平台分支，不綁任何真實網路 / OS IPC。
//   - 事件酬載重用 E6-01 的穩定值型別（`ds::command::CommandArgs`，經 E10-01 傳遞），
//     跨模組邊界不變形。
// 因此可完全以單元測試驗證：發布→全訂閱者收到、主題過濾、多訂閱者、動態訂閱 / 退訂、
// 模組卸載清理、無訂閱者發布等。
#ifndef DS_IPC_E10_04_EVENT_BROADCAST_HPP
#define DS_IPC_E10_04_EVENT_BROADCAST_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "command_bus.hpp"      // E6-01：事件酬載重用穩定值型別（經 E10-01 傳遞）
#include "message_channel.hpp"  // E10-01：底層廣播 / 主題路由通道（PUBLIC 相依）

namespace ds::ipc {

// 協定契約版本標記。事件層承重（模組互通靠它），版本欄位讓消費者可在演進
//（相位 2 換上真實傳輸）時做相容性判斷。定義在 .cpp。
const char* event_broadcast_contract_version() noexcept;

// 事件主題：具名字串（如 "sensor.cpu.updated"、"theme.changed"）。穩定、可讀、
// 跨邊界不變形；不使用數字 topic id —— 與 E10-01 `MessageType` 一致（實為同型別）。
using Topic = MessageType;

// 模組識別：具名字串，對接 E8-04 `Module` 的 manifest.name。訂閱以此歸屬到模組，
// 使模組卸載時能精準清理其全部訂閱。
using ModuleId = std::string;

// ---------------------------------------------------------------------------
// Event — 一則廣播事件：主題 + 酬載 + 來源模組。
//
//   - topic：投遞主題。呼叫端呼叫 publish(topic, event) 時由協定層填入交付給處理器的
//     Event.topic（呼叫端不必自行設定；設了也會被 publish 覆寫為傳入的 topic）。
//   - payload：具名參數酬載（重用 E6-01 `CommandArgs`），需要複合結構時以多個具名參數表達。
//   - source：發布來源模組 id（選填；供訂閱者診斷 / 過濾自己發的事件）。
// ---------------------------------------------------------------------------
struct Event {
    Topic topic;                          // 交付時由 publish 填入（路由主題）
    ds::command::CommandArgs payload;     // 事件酬載（重用 E6-01 穩定值型別）
    ModuleId source;                      // 發布來源模組（選填）

    Event() = default;
    explicit Event(ds::command::CommandArgs p, ModuleId src = {})
        : payload(std::move(p)), source(std::move(src)) {}
};

// 事件處理器：訂閱者收到相符主題的事件時被呼叫（收到的 Event 已填妥 topic）。
using EventHandler = std::function<void(const Event&)>;

// 訂閱識別碼，沿用 E10-01 `SubscriberId`（0 = 無效 / 訂閱失敗，有效值自 1 起）。
using EventSubscriberId = SubscriberId;

// ---------------------------------------------------------------------------
// EventBroadcast — 事件廣播型模組協定。
//
// 契約保證：
//   主題宣告（模組載入時登記其 publish / subscribe 意圖，純內省用，不影響投遞）：
//     - declare_publish(module, topic) / declare_subscribe(module, topic)：登記宣告。
//       空 module / 空 topic 一律忽略（不登記）。
//     - declares_* / declared_*：查詢宣告。
//   訂閱 / 退訂：
//     - subscribe(module, topic, handler)：某模組訂閱某主題；空 module / 空 topic /
//       空 handler 一律拒絕並回 0（不掛上）。回唯一 EventSubscriberId。
//     - unsubscribe(module, id)：退訂該模組名下的某筆訂閱；回是否確有移除
//       （id 非該模組所有 / 未知 id 回 false）。
//     - unsubscribe_module(module)：模組卸載清理 —— 一次註銷該模組的全部訂閱與主題宣告；
//       回移除的訂閱數。
//   廣播：
//     - publish(topic, event)：把事件廣播給所有訂閱該主題的模組，依註冊序投遞；
//       回實際投遞數。無訂閱者回 0（不崩潰、非錯誤）。委派 E10-01 通道扇出。
//   內省：subscriber_count() / subscriber_count(topic) / module_subscription_count(module)
//     / has_module / modules()。
// ---------------------------------------------------------------------------
class EventBroadcast {
public:
    EventBroadcast() = default;

    // 非可複製（持有的通道訂閱回呼捕獲 this；複製會使 this 失聯）。
    EventBroadcast(const EventBroadcast&) = delete;
    EventBroadcast& operator=(const EventBroadcast&) = delete;

    // ---- 主題宣告（模組宣告它 publish / subscribe 哪些主題）----

    // 登記「module 會發布 topic」。空 module / 空 topic 忽略。
    void declare_publish(const ModuleId& module, const Topic& topic) {
        if (module.empty() || topic.empty()) return;
        pub_decls_[module].insert(topic);
    }
    // 登記「module 會訂閱 topic」。空 module / 空 topic 忽略。
    void declare_subscribe(const ModuleId& module, const Topic& topic) {
        if (module.empty() || topic.empty()) return;
        sub_decls_[module].insert(topic);
    }

    bool declares_publish(const ModuleId& module, const Topic& topic) const {
        auto it = pub_decls_.find(module);
        return it != pub_decls_.end() && it->second.count(topic) != 0;
    }
    bool declares_subscribe(const ModuleId& module, const Topic& topic) const {
        auto it = sub_decls_.find(module);
        return it != sub_decls_.end() && it->second.count(topic) != 0;
    }

    // 某模組宣告的 publish / subscribe 主題（有序、去重）。
    std::vector<Topic> declared_publish(const ModuleId& module) const {
        return topics_of(pub_decls_, module);
    }
    std::vector<Topic> declared_subscribe(const ModuleId& module) const {
        return topics_of(sub_decls_, module);
    }

    // ---- 訂閱 / 退訂 ----

    // 某模組訂閱某主題。空 module / 空 topic / 空 handler 一律拒絕並回 0（不掛上）。
    // 委派 E10-01 通道做實際的主題掛載；本層額外把訂閱歸屬到 module 以支援卸載清理。
    EventSubscriberId subscribe(const ModuleId& module, const Topic& topic, EventHandler handler) {
        if (module.empty() || topic.empty() || !handler) return 0;
        // 包一層：E10-01 以 Message 投遞，本層把「當前廣播中的 Event」交給處理器。
        EventSubscriberId id = channel_.subscribe(topic, [this, h = std::move(handler)](const Message&) {
            if (active_) h(*active_);
        });
        if (id != 0) module_subs_[module].push_back(id);
        return id;
    }

    // 退訂該模組名下的某筆訂閱。id 非該模組所有 / 未知 → false（不變更狀態）。
    bool unsubscribe(const ModuleId& module, EventSubscriberId id) {
        auto it = module_subs_.find(module);
        if (it == module_subs_.end()) return false;
        auto& ids = it->second;
        for (auto jt = ids.begin(); jt != ids.end(); ++jt) {
            if (*jt == id) {
                channel_.unsubscribe(id);
                ids.erase(jt);
                if (ids.empty()) module_subs_.erase(it);
                return true;
            }
        }
        return false;
    }

    // 模組卸載清理：一次註銷該模組的全部訂閱與主題宣告。回移除的訂閱數。
    // 對接 E8-04：模組 on_unload 內呼叫此以確保卸載後不再收到 / 殘留訂閱。
    std::size_t unsubscribe_module(const ModuleId& module) {
        std::size_t removed = 0;
        auto it = module_subs_.find(module);
        if (it != module_subs_.end()) {
            for (EventSubscriberId id : it->second) {
                channel_.unsubscribe(id);
                ++removed;
            }
            module_subs_.erase(it);
        }
        // 主題宣告一併清除（模組已卸載，宣告不應殘留）。
        pub_decls_.erase(module);
        sub_decls_.erase(module);
        return removed;
    }

    // ---- 廣播 ----

    // 把事件廣播給所有訂閱該主題的模組，依註冊序投遞。回實際投遞數；
    // 無訂閱者回 0（不崩潰、非錯誤）。委派 E10-01 通道扇出。
    // 巢狀 publish 安全：以存/ 還原保護「當前事件」指標。
    std::size_t publish(const Topic& topic, const Event& event) {
        Event ev = event;
        ev.topic = topic;  // 交付給處理器的 Event 帶正確主題（覆寫呼叫端未設 / 誤設之值）。
        const Event* prev = active_;
        active_ = &ev;
        std::size_t delivered = channel_.publish(Message{topic});  // 僅以主題路由；酬載走 active_。
        active_ = prev;
        return delivered;
    }

    // 便捷：發布無酬載事件。
    std::size_t publish(const Topic& topic) { return publish(topic, Event{}); }

    // ---- 內省 ----

    // 目前訂閱總數（跨所有模組 / 主題）。委派 E10-01 通道。
    std::size_t subscriber_count() const noexcept { return channel_.subscriber_count(); }

    // 指定主題的訂閱者數。委派 E10-01 通道。
    std::size_t subscriber_count(const Topic& topic) const { return channel_.subscriber_count(topic); }

    // 某模組目前的訂閱數。
    std::size_t module_subscription_count(const ModuleId& module) const {
        auto it = module_subs_.find(module);
        return it == module_subs_.end() ? 0 : it->second.size();
    }

    // 是否有某模組的活躍訂閱。
    bool has_module(const ModuleId& module) const {
        return module_subs_.find(module) != module_subs_.end();
    }

    // 目前有活躍訂閱的模組名（有序）。
    std::vector<ModuleId> modules() const {
        std::vector<ModuleId> out;
        out.reserve(module_subs_.size());
        for (const auto& kv : module_subs_) out.push_back(kv.first);
        return out;  // std::map 已排序
    }

private:
    static std::vector<Topic> topics_of(const std::map<ModuleId, std::set<Topic>>& m,
                                        const ModuleId& module) {
        auto it = m.find(module);
        if (it == m.end()) return {};
        return std::vector<Topic>(it->second.begin(), it->second.end());  // std::set 已排序
    }

    MessageChannel channel_;                                // E10-01：底層主題路由 / 扇出。
    std::map<ModuleId, std::vector<EventSubscriberId>> module_subs_;  // 模組 → 其訂閱 id（供卸載清理）。
    std::map<ModuleId, std::set<Topic>> pub_decls_;         // 模組 → 宣告發布的主題。
    std::map<ModuleId, std::set<Topic>> sub_decls_;         // 模組 → 宣告訂閱的主題。
    const Event* active_ = nullptr;                         // 當前廣播中的事件（僅 publish 期間有效）。
};

}  // namespace ds::ipc

#endif  // DS_IPC_E10_04_EVENT_BROADCAST_HPP
