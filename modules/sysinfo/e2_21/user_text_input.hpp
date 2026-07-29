// E2-21 使用者文字輸入值 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「使用者在就地輸入框（E4-15 `TextInputElement`）目前輸入的文字」透過
// **E2-01 的 MetricProvider 介面**暴露為指標——讓 widget 能把使用者輸入（如搜尋框內容、
// 參數輸入）當成一個資料源讀取，而不必各自認得 `TextInputElement` 這個具體型別。這是
// **provider（讀值），非致動器**：本單元只讀輸入框目前的文字，不寫回、不模擬按鍵、不代替
// 使用者輸入——編輯本身完全是 E4-15 的職責，本單元只是把「目前值」接到 E2-01 契約上。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id   = "input.text"
//   - name = "User Text Input"
//   - unit = ""（文字值，無單位）
//   - range = unbounded（文字值無值域）
//   - **可列舉實例 = 各已綁定的輸入框**：每個綁定的 `TextInputElement` 一個
//     `MetricInstance`，instance_id = 呼叫端指定的穩定鍵（key）、label = 人類可讀顯示名、
//     value.text = 該輸入框**目前**文字內容（number 恆為 0.0，消費者讀 text；與 E2-12
//     純文字欄位同慣例）。輸入框目前值無時序意義，history_capacity = 0（誠實表達「無歷史」，
//     同 E2-12 靜態欄位）。
//
// 更新模型（pull，非 push）：本單元不訂閱 E4-15 的編輯事件，避免提供者與具體編輯事件系統
// 耦合。呼叫端在判定「該讀新值」時（例如每次 widget 重繪前，或收到編輯完成通知後）呼叫
// `refresh()`，本單元屆時重新讀各已綁定輸入框的 `text()` 並寫入對應實例——這與 E2-19
// `ProcessMonitorProvider::sample()` / E2-15 `AudioLevelProvider::sample()` 的
// 「register_metrics 建初值、專門方法重讀更新」節奏一致。
//
// 空值 / 無輸入框的誠實處理：
//   - **無輸入框**（從未 `bind()` 過任何輸入框）：`register_metrics()` 仍掛上
//     "input.text" 指標，但 `instance_count()==0`（保守而不崩，同 E2-12 null source）。
//   - **空值**（已綁定的輸入框目前文字為 ""）：對應實例正常存在、`value.valid==true`、
//     `value.text=""`——這是使用者「目前尚未輸入任何字」的真實狀態，**不**等同「無讀值」；
//     與「無輸入框」（指標層級無此實例）明確區分，不混為一談。
//   - `unbind()` 一個先前綁定的輸入框：對應實例保留（供既有參照與消費者穩定走訪），但值
//     設為 `MetricValue::unknown()`（「已解除綁定，目前無讀值」），不再隨 `refresh()` 更新。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只依賴 E2-01 / E4-15 的抽象/具體公開介面**：不觸碰任何真實輸入裝置、鍵盤、IME。
//     `TextInputElement` 本身已是相位 1 的注入式編輯狀態機（無真實鍵盤），本單元進一步
//     只讀其 `text()`，不驅動其編輯。
//   - 無 `#ifdef`、無系統呼叫、無平台分支——換平台一行不動。
#ifndef DS_MODULES_E2_21_USER_TEXT_INPUT_HPP
#define DS_MODULES_E2_21_USER_TEXT_INPUT_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "metric.hpp"               // E2-01 契約（上游，可讀不可改）
#include "text_input_element.hpp"   // E4-15 就地輸入框（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// UserTextInputProvider：把已綁定輸入框的目前文字掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。呼叫端以 `bind()` 把一或多個 E4-15
// `TextInputElement`（如搜尋框、參數輸入框）各自指派一個穩定鍵；`register_metrics()`
// 對註冊表掛上單一指標 "input.text"，其可列舉實例即各已綁定輸入框；`refresh()` 重新讀
// 各輸入框目前文字、更新對應實例。消費者（掛件）只透過 E2-01 的 MetricRegistry / Metric
// 介面走訪，完全不觸及 `TextInputElement` 本身。
class UserTextInputProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼 / 顯示名 / 單位。
    static constexpr const char* kMetricId = "input.text";
    static constexpr const char* kMetricName = "User Text Input";
    static constexpr const char* kUnit = "";
    // 提供者穩定識別碼（供診斷 / 去重 / 溯源）。
    static constexpr const char* kProviderId = "sysinfo.user_text_input";

    UserTextInputProvider() = default;

    // 綁定一個輸入框：key 為跨呼叫穩定的識別碼（用作 MetricInstance::instance_id，如
    // "search_box"）、label 為人類可讀顯示名、element 為欲暴露的 E4-15 輸入框（**不取得
    // 所有權**；element 須存活於本物件之外，且存活期間涵蓋往後的 `refresh()` 呼叫）。
    // 重複的 key 會覆寫既有綁定（保守：以最後一次 bind 為準，避免同 key 兩個輸入框互相打架）。
    // 若 `register_metrics()` 已呼叫過，本次 bind 會立即在既有指標上建立（或更新）對應實例，
    // 並讀入目前文字；否則延後到下次 `register_metrics()` 時一併建立。
    void bind(std::string key, std::string label, const ds::elements::TextInputElement& element);

    // 解除一個輸入框的綁定（依 key）。key 不存在則為 no-op。若指標已建立且該 key 已有實例，
    // 該實例保留（供既有參照穩定走訪）但值設為 `MetricValue::unknown()`（誠實：「已解除
    // 綁定，目前無讀值」），且不再隨 `refresh()` 更新。
    void unbind(const std::string& key);

    // 目前已綁定（尚未 unbind）的輸入框數。
    std::size_t bound_count() const noexcept { return bindings_.size(); }

    std::string provider_id() const override { return kProviderId; }

    // 對註冊表掛上 "input.text" 指標：目前每個已綁定輸入框各建一個實例、讀入其目前文字。
    // 從未 bind() 過任何輸入框 → 指標仍掛上，但 instance_count()==0（保守而不崩）。
    // 重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新讀所有**目前仍綁定**的輸入框之目前文字，寫入對應實例（不推歷史，
    // history_capacity==0）。已 unbind 的既有實例維持 unknown、不受影響。
    // `register_metrics()` 尚未呼叫過（無指標）時為 no-op。
    void refresh();

private:
    struct Binding {
        std::string label;
        const ds::elements::TextInputElement* element = nullptr;
    };

    // 把某個已綁定 key 的目前文字寫入指定實例（number=0.0、text=目前文字，valid=true；
    // 與 E2-12 純文字欄位同慣例）。
    static void write_current_text(ds::metrics::InMemoryMetricInstance& inst,
                                   const ds::elements::TextInputElement& element);

    std::unordered_map<std::string, Binding> bindings_;                 // 目前仍綁定者
    std::vector<std::string> order_;            // key 首次出現順序（決定性走訪；unbind 不移除）
    std::unordered_set<std::string> seen_keys_;  // 曾經 bind 過的 key（供 order_ 去重，
                                                  // 與「目前是否仍綁定」的 bindings_ 分開判斷）
    std::shared_ptr<ds::metrics::InMemoryMetric> metric_;               // register_metrics 後持有
    std::unordered_map<std::string, ds::metrics::InMemoryMetricInstance*> inst_by_key_;  // 非擁有
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_21_USER_TEXT_INPUT_HPP
