// E2-21 使用者文字輸入值 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：綁定表管理（key -> 輸入框指標）、以 E2-01 記憶體內實作把每個已綁定輸入框建成
// 一個實例、register_metrics 建初值、refresh 重讀更新。無 `#ifdef`、無系統呼叫、
// 無任何真實輸入裝置——換平台一行不動。
#include "user_text_input.hpp"

#include <utility>

namespace ds::sysinfo {

void UserTextInputProvider::write_current_text(ds::metrics::InMemoryMetricInstance& inst,
                                               const ds::elements::TextInputElement& element) {
    // 純文字欄位慣例（同 E2-12）：number 恆為 0.0，消費者讀 text。不推歷史（set_value）
    // ——目前值無時序意義，history_capacity==0。
    inst.set_value(ds::metrics::MetricValue::of(0.0, element.text()));
}

void UserTextInputProvider::bind(std::string key, std::string label,
                                 const ds::elements::TextInputElement& element) {
    // 「是否首次出現」以 seen_keys_ 判斷（而非 bindings_，因 unbind 後 bindings_ 會移除
    // 該 key，若在此改用 bindings_ 判斷，rebind 會誤判為「首次」而把 key 重複塞進
    // order_，導致 register_metrics/refresh 走訪到重複 key）。
    const bool is_new = seen_keys_.insert(key).second;

    if (metric_) {
        // 指標已建立：立即在既有指標上建立（新 key）或更新（既有 key）對應實例。
        auto it = inst_by_key_.find(key);
        if (it == inst_by_key_.end()) {
            auto& inst = metric_->add_instance(key, label, /*history_capacity=*/0);
            inst_by_key_.emplace(key, &inst);
            write_current_text(inst, element);
        } else {
            write_current_text(*it->second, element);
        }
    }

    if (is_new) {
        order_.push_back(key);
    }
    bindings_[key] = Binding{std::move(label), &element};
}

void UserTextInputProvider::unbind(const std::string& key) {
    auto it = bindings_.find(key);
    if (it == bindings_.end()) {
        return;  // 未綁定過 → no-op
    }
    bindings_.erase(it);

    // 既有實例（若已建立）保留供穩定走訪，但值設為「無讀值」——誠實表達已解除綁定，
    // 且不再隨 refresh() 更新（因已不在 bindings_ 內）。
    auto inst_it = inst_by_key_.find(key);
    if (inst_it != inst_by_key_.end()) {
        inst_it->second->set_value(ds::metrics::MetricValue::unknown());
    }
}

void UserTextInputProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    metric_ = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, kUnit, ds::metrics::MetricRange::unbounded());
    inst_by_key_.clear();

    // 依綁定建立順序建實例（決定性列舉）。從未 bind() 過 → order_ 為空 → 指標仍掛上，
    // 但 instance_count()==0（保守而不崩，同 E2-12 null source 語意）。
    for (const auto& key : order_) {
        auto it = bindings_.find(key);
        if (it == bindings_.end()) {
            continue;  // 理論上不會發生：unbind 不會移除 order_ 項；防禦性略過。
        }
        auto& inst = metric_->add_instance(key, it->second.label, /*history_capacity=*/0);
        inst_by_key_.emplace(key, &inst);
        write_current_text(inst, *it->second.element);
    }

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(metric_);
}

void UserTextInputProvider::refresh() {
    if (!metric_) {
        return;  // register_metrics 尚未呼叫過 → no-op
    }
    // 只更新目前仍綁定者；已 unbind 的既有實例維持 unknown、不受影響。
    for (const auto& key : order_) {
        auto bind_it = bindings_.find(key);
        if (bind_it == bindings_.end()) {
            continue;  // 已 unbind
        }
        auto inst_it = inst_by_key_.find(key);
        if (inst_it == inst_by_key_.end()) {
            continue;  // 防禦性：理論上已綁定者必有對應實例
        }
        write_current_text(*inst_it->second, *bind_it->second.element);
    }
}

}  // namespace ds::sysinfo
