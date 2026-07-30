// E10-01 本機 IPC 訊息投遞 — 平台中立契約（行程內佇列模型）
//
// 本機 IPC：行程內 / 同機的訊息傳遞通道，供獨立行程 widget 宿主等使用。
// 相位 1 刻意採「行程內佇列模型」——不綁真實 socket / pipe / OS IPC，只有純邏輯的
// 訊息路由與投遞語意。等真實傳輸層（相位 2）到位時，本契約即為其上的通道抽象，
// 傳輸細節換掉、投遞語意不動。
//
// 本單元屬 engine 層（平台中立純邏輯）：
//   - 無 `#ifdef` / `_WIN32` / `cocoa` 等平台分支，不綁任何真實 OS IPC。
//   - 訊息酬載重用 E6-01 的穩定值型別（`ds::command::CommandArgs`），跨模組邊界不變形；
//     故本機 IPC 可直接攜帶 E6-01 具名命令（`Command`），與命令匯流排無縫搭配。
//   - 兩種投遞模型並存：
//       * 點對點佇列：`send` 入列 / `receive` 出列（FIFO），單一消費者拉取。
//       * 發布訂閱：`subscribe(type, cb)` 掛上 / `publish(msg)` 依註冊序投遞給相符訂閱者。
// 因此可完全以單元測試驗證：送收、多訂閱、投遞順序、無訂閱者、退訂等。
#ifndef DS_IPC_E10_01_MESSAGE_CHANNEL_HPP
#define DS_IPC_E10_01_MESSAGE_CHANNEL_HPP

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "command_bus.hpp"  // E6-01：重用穩定值型別 / 具名命令（PUBLIC 相依）

namespace ds::ipc {

// 訊息型別：具名字串（如 "widget.refresh"、"command"）。穩定、可讀、跨邊界不變形；
// 不使用數字 topic id（避免耦合編號），與 E6-01 CommandId 的取捨一致。
using MessageType = std::string;

// 契約版本標記。本機 IPC 通道為承重抽象（widget 宿主等消費），版本欄位讓消費者可在
// 演進（相位 2 換上真實傳輸）時做相容性判斷。定義在 .cpp。
const char* contract_version() noexcept;

// ---------------------------------------------------------------------------
// Message — 一則 IPC 訊息：型別 + 酬載。
//
// 酬載重用 E6-01 的 `CommandArgs`（穩定值型別的具名字典），使訊息可直接攜帶命令參數；
// 需要複合結構時以多個具名參數表達，而非在此塞任意型別（與 E6-01 契約一致）。
// ---------------------------------------------------------------------------
struct Message {
    MessageType type;                    // 訊息型別（路由鍵）
    ds::command::CommandArgs payload;    // 具名參數酬載（重用 E6-01 穩定值型別）

    Message() = default;
    explicit Message(MessageType t) : type(std::move(t)) {}
    Message(MessageType t, ds::command::CommandArgs p)
        : type(std::move(t)), payload(std::move(p)) {}

    // 便捷建構：以 E6-01 具名命令建一則訊息（型別取命令 id、酬載取命令參數）。
    // 讓本機 IPC 直接承載命令匯流排的 Command，投遞後由接收端交給 CommandBus 分派。
    static Message from_command(const ds::command::Command& cmd) {
        return Message{cmd.id, cmd.args};
    }
};

// 訂閱識別碼；0 保留為「無效 / 訂閱失敗」，有效值自 1 起。
using SubscriberId = std::uint64_t;

// 訂閱者回呼：收到相符訊息時被呼叫。
using Subscriber = std::function<void(const Message&)>;

// ---------------------------------------------------------------------------
// MessageChannel — 本機 IPC 訊息通道。
//
// 契約保證：
//   發布訂閱：
//     - subscribe(type, cb)：訂閱指定型別；空型別 / 空回呼回 0（失敗，不掛上）。
//     - subscribe_all(cb)：訂閱所有型別（如診斷 / 記錄用）。
//     - publish(msg)：依「註冊順序」投遞給相符訂閱者，回傳實際投遞數；
//       無相符訂閱者回 0（不崩潰、非錯誤）。
//     - unsubscribe(id)：移除訂閱；回是否確有移除。
//   點對點佇列：
//     - send(msg)：入列（FIFO）。
//     - receive()：出列最前一則；空佇列回 std::nullopt（不崩潰）。
//   兩模型互不干擾：publish 不入列、send 不觸發訂閱者。
// ---------------------------------------------------------------------------
class MessageChannel {
public:
    MessageChannel() = default;

    // ---- 發布訂閱 ----

    // 訂閱指定型別。空型別或空回呼一律拒絕並回 0（不掛上、不變更狀態）。
    SubscriberId subscribe(MessageType type, Subscriber cb) {
        if (type.empty() || !cb) return 0;
        SubscriberId id = next_id_++;
        subs_.push_back(Sub{id, std::move(type), /*all=*/false, std::move(cb)});
        return id;
    }

    // 訂閱所有型別（萬用訂閱者）。空回呼拒絕並回 0。
    SubscriberId subscribe_all(Subscriber cb) {
        if (!cb) return 0;
        SubscriberId id = next_id_++;
        subs_.push_back(Sub{id, MessageType{}, /*all=*/true, std::move(cb)});
        return id;
    }

    // 取消訂閱。回是否確有移除（未知 id 回 false）。
    bool unsubscribe(SubscriberId id) {
        for (auto it = subs_.begin(); it != subs_.end(); ++it) {
            if (it->id == id) {
                subs_.erase(it);
                return true;
            }
        }
        return false;
    }

    // 發布一則訊息：依註冊順序投遞給「型別相符」及「萬用」訂閱者。
    // 回傳實際投遞的訂閱者數；無相符者回 0（不崩潰、不視為錯誤）。
    std::size_t publish(const Message& msg) const {
        std::size_t delivered = 0;
        for (const auto& s : subs_) {
            if (s.all || s.type == msg.type) {
                s.cb(msg);
                ++delivered;
            }
        }
        return delivered;
    }

    // 目前訂閱者總數。
    std::size_t subscriber_count() const noexcept { return subs_.size(); }

    // 指定型別的訂閱者數（含萬用訂閱者，因其會收到該型別）。
    std::size_t subscriber_count(const MessageType& type) const {
        std::size_t n = 0;
        for (const auto& s : subs_) {
            if (s.all || s.type == type) ++n;
        }
        return n;
    }

    // ---- 點對點佇列（send / receive）----

    // 入列一則訊息（FIFO）。
    void send(Message msg) { queue_.push_back(std::move(msg)); }

    // 出列最前一則訊息；空佇列回 std::nullopt（不崩潰）。
    std::optional<Message> receive() {
        if (queue_.empty()) return std::nullopt;
        Message front = std::move(queue_.front());
        queue_.pop_front();
        return front;
    }

    // 佇列中待收訊息數。
    std::size_t pending() const noexcept { return queue_.size(); }
    bool has_pending() const noexcept { return !queue_.empty(); }

private:
    struct Sub {
        SubscriberId id;
        MessageType type;   // all=true 時忽略
        bool all;           // 是否為萬用（所有型別）訂閱者
        Subscriber cb;
    };

    std::vector<Sub> subs_;         // 有序：保證投遞依註冊序（決定性）
    SubscriberId next_id_ = 1;      // 遞增；0 保留為無效
    std::deque<Message> queue_;     // 點對點 FIFO 佇列
};

}  // namespace ds::ipc

#endif  // DS_IPC_E10_01_MESSAGE_CHANNEL_HPP
