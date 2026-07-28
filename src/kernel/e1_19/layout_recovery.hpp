// E1-19 顯示器熱插拔復原 — 平台中立介面
//
// 處理顯示器熱插拔（接上 / 拔除螢幕、解析度 / 排列變更）後的**佈局復原**：
//   - 以 E5-08 系統事件（DisplayChanged）偵測顯示器拓撲變更。
//   - 以每一「顯示器組態」為單位記憶各具名螢幕的視窗 / 元件佈局。
//   - 拓撲恢復（同一組具名螢幕再次出現）時還原對應佈局；全新顯示器給預設佈局；
//     被拔除的螢幕其記錄保留於原組態，重接即復原。
//
// 相位 1（Mac / null 期）約束：
//   - 只有平台中立介面 + 注入式來源；**絕不**接真實顯示器 API（無 CGDisplay /
//     EnumDisplayMonitors / cocoa / `#ifdef`），**不建 backend/win32|cocoa**。
//   - 拓撲由 E1-18 的具名螢幕抽象（ScreenRegistry）表示，來源可注入（TopologyProvider）；
//     系統事件由 E5-08 的可注入來源（NullSystemEventSource::inject）驅動。本單元絕不查 OS。
//   - 狀態全存記憶體。跨平台性由 API 面約束保證，不由語言保證。
//
// 硬約束（NFR-02）：本介面**不得出現絕對座標與數字 z-order**。
//   - 顯示器組態一律以**具名螢幕識別碼**導出的「拓撲簽章」指涉，不以 index / (x,y) 指涉。
//   - 佈局表示（Layout）刻意留給消費者以泛型提供（沿用 E1-18 PerScreen<T> 的哲學），
//     本單元自身不引入任何座標型別；建議消費者亦以具名佈局權杖表達。
// 硬約束（NFR-03）：能力閘控式查詢一律以 has()/find() 保護，未知一律保守回預設。
#ifndef DS_KERNEL_E1_19_LAYOUT_RECOVERY_HPP
#define DS_KERNEL_E1_19_LAYOUT_RECOVERY_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "named_screens.hpp"  // E1-18 ScreenRegistry / PerScreen / ScreenId / Screen
#include "system_event.hpp"   // E5-08 SystemEventSource / SystemEvent / SubscriptionId

namespace ds::kernel {

// E5-08 系統事件型別位於 ds::events；於本命名空間內以別名引用，行文簡潔且不污染全域。
using ds::events::SubscriptionId;
using ds::events::SystemEvent;
using ds::events::SystemEventSource;
using ds::events::SystemEventType;

// 由具名螢幕拓撲導出的穩定「拓撲簽章」。
//
// 僅以具名 ScreenId 構成（排序後串接），永不含座標 / 數字 index / z-order（NFR-02）。
// 排序使其與列舉順序無關：相同的一組具名螢幕（不論後端以何順序列舉）→ 相同簽章，
// 故「拔除後重接同一組螢幕」= 拓撲恢復可被辨識，據以還原對應組態的佈局。
// 空拓撲回空字串。
std::string topology_key(const ScreenRegistry& topology);

// 顯示器熱插拔的佈局復原管理器（平台中立、事件驅動）。
//
// Layout 為呼叫端提供的平台中立佈局表示。本單元不定義座標型別——沿用 E1-18 PerScreen<T>
// 的泛型哲學，把「佈局長什麼樣」留給消費者，本單元只負責「哪個顯示器組態記哪份佈局、
// 何時觸發復原」的邏輯。
//
// 事件驅動流程：
//   1. 建構時以注入的 TopologyProvider 取一次當前拓撲，算出初始拓撲簽章。
//   2. 訂閱 E5-08 SystemEventSource，僅對 DisplayChanged 反應（其餘系統事件忽略）。
//   3. DisplayChanged 到達 → 重新以 TopologyProvider 取當前拓撲、重算簽章 → 呼叫可選的
//      on_recover 回呼，消費者於回呼中對每個具名螢幕呼叫 layout_for() 取回應套用的佈局。
//
// 復原語意：
//   - 復原往返：於組態 A 記錄佈局 → 切到 B → 切回 A，layout_for() 回 A 當初記錄的佈局。
//   - 新顯示器預設：當前組態中無記錄的螢幕（含全新顯示器）→ layout_for() 回預設佈局。
//   - 拔除處理：被拔除的螢幕不在當前拓撲；其在原組態的記錄仍保留（多組態記憶），重接即復原。
//
// 相位 1：來源與拓撲皆注入式，不查 OS、不含平台分支、不含座標。
template <typename Layout>
class LayoutRecoveryManager {
public:
    // 提供「當前具名螢幕拓撲」的來源。相位 1 由測試注入；相位 2 由真實後端查 OS 後回填。
    using TopologyProvider = std::function<ScreenRegistry()>;
    // 復原回呼：每次 DisplayChanged 重算拓撲後被呼叫，帶入新的當前拓撲供消費者重套佈局。
    using RecoverCallback = std::function<void(const ScreenRegistry&)>;

    // 以（可注入的）系統事件來源、拓撲來源、預設佈局建構。建構時即訂閱 DisplayChanged。
    LayoutRecoveryManager(SystemEventSource& source, TopologyProvider provider,
                          Layout default_layout)
        : source_(&source),
          provider_(std::move(provider)),
          default_layout_(std::move(default_layout)) {
        refresh_active_();  // 取初始當前拓撲
        // 僅 DisplayChanged 觸發復原；其餘系統事件忽略。
        sub_ = source_->subscribe([this](const SystemEvent& event) {
            if (event.type == SystemEventType::DisplayChanged) {
                handle_display_changed_();
            }
        });
    }

    // 解構時解除訂閱（來源生命週期由外部管理，本類僅持有非擁有指標）。
    ~LayoutRecoveryManager() {
        if (source_ != nullptr && sub_ != 0) {
            source_->unsubscribe(sub_);
        }
    }

    // 不可複製 / 搬移：持有事件訂閱與來源指標，複製語意不明確故禁止（保守）。
    LayoutRecoveryManager(const LayoutRecoveryManager&) = delete;
    LayoutRecoveryManager& operator=(const LayoutRecoveryManager&) = delete;

    // 當前拓撲簽章（具名導出，NFR-02）。
    const std::string& active_topology_key() const noexcept { return active_key_; }

    // 當前拓撲的具名螢幕識別碼（宣告順序，來自 E1-18 列舉；永不暴露數字 index）。
    std::vector<ScreenId> active_screen_ids() const { return active_.ids(); }

    // 該具名螢幕是否存在於當前拓撲。**未知回 false（保守）**。
    bool is_active_screen(const ScreenId& id) const { return active_.is_known(id); }

    // 記錄某具名螢幕在「當前顯示器組態」下的佈局（消費者於使用者排列視窗 / 元件後呼叫）。
    // 同一（組態, 螢幕）重複記錄則覆蓋（後記錄者為準）。
    void record(const ScreenId& id, Layout layout) {
        config_for_(active_key_).set(id, std::move(layout));
    }

    // 當前組態下該具名螢幕「應套用」的佈局：
    //   有記錄 → 回記錄（復原）；無記錄（含全新顯示器）→ 回預設佈局。
    // 以 find() 保護（NFR-03），未知一律安全回預設。
    Layout layout_for(const ScreenId& id) const {
        const PerScreen<Layout>* per = find_config_(active_key_);
        if (per != nullptr) {
            const Layout* v = per->find(id);
            if (v != nullptr) {
                return *v;
            }
        }
        return default_layout_;
    }

    // 當前組態下該具名螢幕是否已有記錄佈局（false ⇒ layout_for 會落到預設）。
    bool has_recorded(const ScreenId& id) const {
        const PerScreen<Layout>* per = find_config_(active_key_);
        return per != nullptr && per->has(id);
    }

    // 當前組態下已記錄佈局的具名螢幕（設定順序）。
    std::vector<ScreenId> recorded_screen_ids() const {
        const PerScreen<Layout>* per = find_config_(active_key_);
        return per != nullptr ? per->ids() : std::vector<ScreenId>{};
    }

    // 是否記得某拓撲簽章的組態（供診斷 / 測試）。
    bool remembers_topology(const std::string& key) const {
        return find_config_(key) != nullptr;
    }

    // 已記憶的顯示器組態數量（多組態記憶）。
    std::size_t topology_count() const noexcept { return store_.size(); }

    // 設定復原回呼；空回呼即停用（DisplayChanged 仍會更新當前拓撲，只是不通知）。
    void set_on_recover(RecoverCallback cb) { on_recover_ = std::move(cb); }

    // 預設佈局（新顯示器 / 無記錄螢幕所回傳者）。
    const Layout& default_layout() const noexcept { return default_layout_; }

    // 清空所有組態記憶（不影響訂閱與當前拓撲）。
    void clear() { store_.clear(); }

private:
    // 單一顯示器組態：拓撲簽章 → 該組態下各具名螢幕的佈局。
    struct Config {
        std::string key;
        PerScreen<Layout> layouts;
    };

    // 取（必要時建立）某拓撲簽章對應的組態佈局表。線性小容量（組態數量少）。
    PerScreen<Layout>& config_for_(const std::string& key) {
        for (auto& c : store_) {
            if (c.key == key) {
                return c.layouts;
            }
        }
        store_.push_back(Config{key, PerScreen<Layout>{}});
        return store_.back().layouts;
    }

    // 查某拓撲簽章的組態佈局表；**未知回 nullptr（保守）**。
    const PerScreen<Layout>* find_config_(const std::string& key) const {
        for (const auto& c : store_) {
            if (c.key == key) {
                return &c.layouts;
            }
        }
        return nullptr;
    }

    // 以 TopologyProvider 重取當前拓撲並重算簽章；provider 為空則視為空拓撲（保守）。
    void refresh_active_() {
        active_ = provider_ ? provider_() : ScreenRegistry(std::vector<Screen>{});
        active_key_ = topology_key(active_);
    }

    // DisplayChanged 到達：重算當前拓撲，通知消費者重套佈局。
    void handle_display_changed_() {
        refresh_active_();
        if (on_recover_) {
            on_recover_(active_);
        }
    }

    SystemEventSource* source_;      // 非擁有；生命週期由外部管理
    TopologyProvider provider_;      // 注入的當前拓撲來源
    Layout default_layout_;          // 新顯示器 / 無記錄螢幕的預設佈局
    ScreenRegistry active_{std::vector<Screen>{}};  // 當前拓撲快照
    std::string active_key_;         // 當前拓撲簽章
    std::vector<Config> store_;      // 多組態記憶：拓撲簽章 → 各螢幕佈局
    RecoverCallback on_recover_;     // 可選復原回呼
    SubscriptionId sub_ = 0;         // E5-08 訂閱代號（0 = 無效）
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_19_LAYOUT_RECOVERY_HPP
