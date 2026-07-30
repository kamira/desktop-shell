// E10-02 低延遲同機通道 — 平台中立契約（建於 E10-01 訊息通道之上）
//
// 同機（本機行程內 / 跨行程）的「低延遲」點對點與請求-回應通道抽象。E10-01 提供了
// 中立的訊息投遞語意（FIFO 佇列 + 發布訂閱）；本單元在其上疊加低延遲傳遞所需的語意：
//   - 優先佇列：高優先訊息先出（同優先級內 FIFO），讓延遲敏感訊息插隊。
//   - 背壓處理：佇列有界，滿載時以明確策略處置（拒絕 / 汰換最舊低優先），絕不阻塞、不崩潰。
//   - 批次收送：一次搬移多則，攤提每則固定成本（批次 = 低延遲的常見手段）。
//   - 零拷貝語意：全程 std::move 搬移 `Envelope`，不複製酬載。
//   - 請求-回應：快速的同機 RPC 抽象（`request_response`）。
//   - 延遲量測：以「可注入的邏輯時脈」（tick）量測，不依賴 wall-clock —— 相位 1 平台中立，
//     延遲以邏輯單位可決定性地被測。
//
// 相位 1 刻意「平台中立」：不綁任何真實 IPC（無 shared memory / socket / mmap / `#ifdef`）。
// 低延遲語意全以「記憶體內佇列 + 邏輯時脈」實現；等真實傳輸層（相位 2）到位時，本契約即為
// 其上的通道抽象——傳輸換掉、優先/背壓/延遲語意不動。
//
// 本單元屬 engine 層（平台中立純邏輯）：無 `_WIN32` / `cocoa` / `__APPLE__` 平台分支，
// 訊息酬載沿用 E10-01 `Message`（其酬載重用 E6-01 `CommandArgs`），與命令匯流排無縫搭配。
#ifndef DS_IPC_E10_02_LOW_LATENCY_CHANNEL_HPP
#define DS_IPC_E10_02_LOW_LATENCY_CHANNEL_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "message_channel.hpp"  // E10-01：Message / MessageChannel（其酬載重用 E6-01 CommandArgs）

namespace ds::ipc {

// 契約版本標記。本機低延遲通道為承重抽象，版本欄位讓消費者於演進（相位 2 換上真實傳輸）時
// 做相容性判斷。定義在 .cpp。以 low_latency_ 前綴避免與 E10-01 的 contract_version() 撞名。
const char* low_latency_contract_version() noexcept;

// ---------------------------------------------------------------------------
// LogicalClock — 可注入的邏輯時脈（單調遞增 tick）。
//
// 延遲以「tick 差」量測，而非 wall-clock：相位 1 平台中立、可決定性測試。呼叫端以 advance()
// 手動推進時間（如模擬傳輸耗費 N tick），使延遲量測完全可控、可重現。
// ---------------------------------------------------------------------------
class LogicalClock {
public:
    using Tick = std::uint64_t;

    Tick now() const noexcept { return tick_; }

    // 推進時脈；delta 為推進量（預設 1）。回傳推進後的 tick。
    Tick advance(Tick delta = 1) noexcept {
        tick_ += delta;
        return tick_;
    }

    void reset() noexcept { tick_ = 0; }

private:
    Tick tick_ = 0;
};

// ---------------------------------------------------------------------------
// Priority — 通道優先級。數值越大越優先；同優先級內維持 FIFO。
// ---------------------------------------------------------------------------
enum class Priority : std::uint8_t {
    Low = 0,
    Normal = 1,
    High = 2,
};

// 優先級數量（供內部分帶佇列使用）。
inline constexpr std::size_t kPriorityLevels = 3;

// ---------------------------------------------------------------------------
// SendStatus — send 的結果（背壓明確化，永不阻塞 / 崩潰）。
// ---------------------------------------------------------------------------
enum class SendStatus {
    Accepted,        // 已入列
    AcceptedEvicted, // 已入列，但為騰位汰換了一則較舊 / 較低優先的訊息（DropOldestLowPriority）
    RejectedFull,    // 佇列已滿且策略為拒絕（或無可汰換者）—— 背壓訊號
};

// send 是否成功入列（AcceptedEvicted 亦視為成功）。
inline bool send_accepted(SendStatus s) noexcept {
    return s == SendStatus::Accepted || s == SendStatus::AcceptedEvicted;
}

// ---------------------------------------------------------------------------
// BackpressurePolicy — 佇列滿載時的處置策略。
//   Reject                  ：拒收新訊息（回 RejectedFull），既有內容不動 —— 純背壓。
//   DropOldestLowPriority   ：為新訊息騰位，汰換「優先級 <= 新訊息」帶中最低帶的最舊一則；
//                             若佇列內全部訊息皆嚴格高於新訊息，則改為拒收新訊息（保護高優先）。
// ---------------------------------------------------------------------------
enum class BackpressurePolicy {
    Reject,
    DropOldestLowPriority,
};

// ---------------------------------------------------------------------------
// Envelope — 低延遲通道的傳輸單元：E10-01 `Message` + 低延遲中繼資料。
//
// 沿用 E10-01 `Message`（型別 + `CommandArgs` 酬載），故可直接承載 E6-01 具名命令。
// 全程以 std::move 搬移（零拷貝語意）。
// ---------------------------------------------------------------------------
struct Envelope {
    Message message;                        // 沿用 E10-01 訊息（酬載重用 E6-01 CommandArgs）
    Priority priority = Priority::Normal;    // 投遞優先級
    LogicalClock::Tick enqueued_tick = 0;    // send 接受當下的邏輯時刻（供延遲量測）
    std::uint64_t seq = 0;                   // 全域入列序號（同帶 FIFO / 診斷用）

    Envelope() = default;
    Envelope(Message m, Priority p) : message(std::move(m)), priority(p) {}
};

// ---------------------------------------------------------------------------
// LatencyStats — 邏輯延遲統計（tick 為單位）。
//
// 延遲 = 出列時刻 − 入列時刻（皆取自邏輯時脈）。提供最近 / 最大 / 累計 / 平均。
// ---------------------------------------------------------------------------
struct LatencyStats {
    std::uint64_t delivered = 0;          // 已出列（含 request_response）之訊息數
    LogicalClock::Tick last = 0;          // 最近一次出列的延遲
    LogicalClock::Tick max = 0;           // 觀測到的最大延遲
    std::uint64_t total = 0;              // 延遲累計（供平均）

    // 平均延遲（tick）；尚無投遞時回 0.0。
    double average() const noexcept {
        return delivered == 0 ? 0.0 : static_cast<double>(total) / static_cast<double>(delivered);
    }
};

// 請求-回應處理器：接收請求訊息、產生回應訊息（快速同機 RPC）。
using Responder = std::function<Message(const Message& request)>;

// ---------------------------------------------------------------------------
// LowLatencyChannel — 低延遲同機通道。
//
// 契約保證：
//   點對點（優先 + 背壓 + 批次）：
//     - send(msg, prio)：依優先級入列；有界時依 BackpressurePolicy 處置，回 SendStatus。
//     - send_batch(...)：批次入列，回實際接受數。
//     - try_receive()：出列「最高優先、同優先級 FIFO」的一則；空佇列回 std::nullopt（不崩潰）。
//     - receive_batch(max)：一次出列至多 max 則（同上排序）。
//     - 出列時以邏輯時脈記錄延遲（last/max/total/average）。
//   請求-回應：
//     - request_response(req, responder)：快速同機 RPC；記錄邏輯往返延遲；無回應器回 nullopt。
//   有界 / 背壓：
//     - capacity==0 表無界；否則滿載時 send 依策略拒絕或汰換，永不阻塞、不崩潰。
//   可注入時脈：建構時傳入 LogicalClock*；未提供則使用內部自有時脈。
// ---------------------------------------------------------------------------
class LowLatencyChannel {
public:
    // clock：可注入的邏輯時脈（nullptr → 使用內部自有時脈）。
    // capacity：0 表無界；>0 為佇列容量上限（背壓門檻）。
    // policy：滿載處置策略。
    explicit LowLatencyChannel(LogicalClock* clock = nullptr,
                               std::size_t capacity = 0,
                               BackpressurePolicy policy = BackpressurePolicy::Reject)
        : clock_(clock ? clock : &owned_clock_), capacity_(capacity), policy_(policy) {}

    // ---- 點對點：優先送 ----

    // 入列一則訊息（指定優先級）。回 SendStatus（背壓明確化）。
    SendStatus send(Message msg, Priority prio = Priority::Normal) {
        return enqueue(Envelope{std::move(msg), prio});
    }

    // 入列一個已組好的 Envelope（保留其 priority；enqueued_tick / seq 由通道覆寫）。
    SendStatus send(Envelope env) { return enqueue(std::move(env)); }

    // 批次入列：回實際接受（含汰換型接受）的則數。一旦某則被拒（RejectedFull）即停止，
    // 回已接受數（背壓下的部分接受，呼叫端可據此得知未送出的尾段）。
    std::size_t send_batch(std::vector<Envelope> batch) {
        std::size_t accepted = 0;
        for (auto& e : batch) {
            if (!send_accepted(enqueue(std::move(e)))) break;
            ++accepted;
        }
        return accepted;
    }

    // ---- 點對點：優先收 ----

    // 出列最高優先（同優先級 FIFO）的一則；空佇列回 std::nullopt（不崩潰）。
    // 出列時以邏輯時脈記錄延遲。
    std::optional<Envelope> try_receive() {
        for (std::size_t lvl = kPriorityLevels; lvl-- > 0;) {
            auto& band = bands_[lvl];
            if (!band.empty()) {
                Envelope e = std::move(band.front());
                band.pop_front();
                --size_;
                record_latency(clock_->now() - e.enqueued_tick);
                return e;
            }
        }
        return std::nullopt;
    }

    // 批次出列：一次取至多 max 則（最高優先優先，同優先級 FIFO）；不足則取盡。
    std::vector<Envelope> receive_batch(std::size_t max) {
        std::vector<Envelope> out;
        out.reserve(max < size_ ? max : size_);
        for (std::size_t i = 0; i < max; ++i) {
            auto e = try_receive();
            if (!e) break;
            out.push_back(std::move(*e));
        }
        return out;
    }

    // ---- 請求-回應（快速同機 RPC）----

    // 送出請求並取回回應：呼叫已註冊回應器 responder(request)，記錄邏輯往返延遲。
    // 呼叫端可在 responder 內或前後以 clock.advance() 模擬傳輸耗時。空回應器回 std::nullopt。
    std::optional<Message> request_response(const Message& request, const Responder& responder) {
        if (!responder) {
            ++rr_rejected_;
            return std::nullopt;
        }
        LogicalClock::Tick t0 = clock_->now();
        Message resp = responder(request);
        LogicalClock::Tick t1 = clock_->now();
        record_latency(t1 - t0);
        ++rr_delivered_;
        return resp;
    }

    // ---- 觀測 ----

    std::size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    std::size_t capacity() const noexcept { return capacity_; }      // 0 = 無界
    bool bounded() const noexcept { return capacity_ != 0; }
    bool is_full() const noexcept { return capacity_ != 0 && size_ >= capacity_; }
    BackpressurePolicy policy() const noexcept { return policy_; }

    // 指定優先級當前待收數。
    std::size_t size(Priority prio) const noexcept { return bands_[index_of(prio)].size(); }

    const LatencyStats& latency() const noexcept { return latency_; }
    std::uint64_t rejected_count() const noexcept { return rejected_; }   // 背壓拒收累計
    std::uint64_t evicted_count() const noexcept { return evicted_; }     // 汰換累計
    std::uint64_t rr_delivered() const noexcept { return rr_delivered_; } // 請求-回應成功數
    std::uint64_t rr_rejected() const noexcept { return rr_rejected_; }   // 請求-回應失敗數

    LogicalClock& clock() noexcept { return *clock_; }
    const LogicalClock& clock() const noexcept { return *clock_; }

private:
    static std::size_t index_of(Priority p) noexcept { return static_cast<std::size_t>(p); }

    SendStatus enqueue(Envelope env) {
        if (is_full()) {
            if (policy_ == BackpressurePolicy::Reject || !evict_for(env.priority)) {
                ++rejected_;
                return SendStatus::RejectedFull;
            }
            // evict_for 已騰出一位，續行入列並標記為汰換型接受。
            place(std::move(env));
            return SendStatus::AcceptedEvicted;
        }
        place(std::move(env));
        return SendStatus::Accepted;
    }

    // 實際落位：蓋章 enqueued_tick / seq，推入對應優先帶。
    void place(Envelope env) {
        env.enqueued_tick = clock_->now();
        env.seq = next_seq_++;
        bands_[index_of(env.priority)].push_back(std::move(env));
        ++size_;
    }

    // 為 incoming 優先級騰位：於 [Low .. incoming]（含）帶中，找最低的非空帶，汰換其最舊一則。
    // 成功回 true（size_ 已減一）；若這些帶皆空（佇列內全部嚴格高於 incoming）回 false。
    bool evict_for(Priority incoming) {
        std::size_t limit = index_of(incoming);
        for (std::size_t lvl = 0; lvl <= limit; ++lvl) {
            auto& band = bands_[lvl];
            if (!band.empty()) {
                band.pop_front();
                --size_;
                ++evicted_;
                return true;
            }
        }
        return false;
    }

    void record_latency(LogicalClock::Tick lat) {
        latency_.last = lat;
        if (lat > latency_.max) latency_.max = lat;
        latency_.total += lat;
        ++latency_.delivered;
    }

    LogicalClock owned_clock_{};                          // 未注入時使用
    LogicalClock* clock_;                                 // 指向注入或內部時脈
    std::size_t capacity_;                                // 0 = 無界
    BackpressurePolicy policy_;
    std::array<std::deque<Envelope>, kPriorityLevels> bands_{};  // 分優先帶（High..Low）
    std::size_t size_ = 0;                                // 全帶總數
    std::uint64_t next_seq_ = 1;                          // 入列序號（自 1）
    LatencyStats latency_{};
    std::uint64_t rejected_ = 0;
    std::uint64_t evicted_ = 0;
    std::uint64_t rr_delivered_ = 0;
    std::uint64_t rr_rejected_ = 0;
};

}  // namespace ds::ipc

#endif  // DS_IPC_E10_02_LOW_LATENCY_CHANNEL_HPP
