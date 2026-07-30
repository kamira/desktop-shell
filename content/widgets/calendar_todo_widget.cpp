// content/widgets/c2_09/calendar_todo_widget.cpp — C2-09 行事曆 / 待辦 widget 實作。
// 見 calendar_todo_widget.hpp。平台中立、純資料 / 邏輯組裝，無真實 GUI / 檔案 I/O。
#include "calendar_todo_widget.hpp"

#include <utility>

namespace ds::widgets {

namespace {

using ds::format::Document;
using ds::format::ForestResult;
using ds::format::Item;
using ds::format::ParseResult;
using ds::format::Value;

// 森林 / 日期分組的保留鍵：E7-01 根層放置整份行事曆 / 待辦森林的鍵。
constexpr const char* kDaysKey = "days";
// 事項附帶酬載（E7-13 `value`）保留鍵：完成狀態。
constexpr const char* kDoneKey = "done";
// 日期分組 id 前綴（與呼叫端事項 id 的命名空間區隔，避免巧合碰撞；仍受 E7-13 全域唯一檢查保護）。
constexpr const char* kDateGroupPrefix = "day:";

// 供 list_items() 對「不存在的日期分組」回傳的穩定空清單（非錯誤語意）。
const std::vector<Item>& empty_items() {
    static const std::vector<Item> kEmpty{};
    return kEmpty;
}

// 讀取事項 value（Map{done:bool}）目前的完成狀態；缺 done 鍵或型別不符視為 false（不靜默失敗，
// 因為 value 本為呼叫端可自由擴充的酬載——本單元只讀取自己認得的 done 鍵，其餘鍵原樣保留）。
bool read_done(const Value& value) {
    if (!value.is_map()) {
        return false;
    }
    const Value* done = value.find(kDoneKey);
    return done != nullptr && done->is_bool() && done->as_bool();
}

}  // namespace

const char* to_string(TodoStatus s) noexcept {
    switch (s) {
        case TodoStatus::Ok:
            return "Ok";
        case TodoStatus::Invalid:
            return "Invalid";
        case TodoStatus::NotFound:
            return "NotFound";
        case TodoStatus::DuplicateId:
            return "DuplicateId";
        case TodoStatus::ParseError:
            return "ParseError";
    }
    return "Unknown";
}

CalendarTodoWidget::CalendarTodoWidget(std::string id, ds::kernel::KernelBackend& backend,
                                       ds::kernel::LayerStack& layers,
                                       const ds::render::FontMetrics& metrics)
    : shell_(std::move(id), backend, layers), metrics_(metrics) {}

std::string CalendarTodoWidget::date_group_id(const std::string& date) {
    return kDateGroupPrefix + date;
}

bool CalendarTodoWidget::is_date_group_id(const std::string& id) const {
    for (const Item& d : days_) {
        if (d.id() == id) {
            return true;
        }
    }
    return false;
}

Item* CalendarTodoWidget::find_date_group(const std::string& date) {
    const std::string gid = date_group_id(date);
    for (Item& d : days_) {
        if (d.id() == gid) {
            return &d;
        }
    }
    return nullptr;
}

const Item* CalendarTodoWidget::find_date_group(const std::string& date) const {
    const std::string gid = date_group_id(date);
    for (const Item& d : days_) {
        if (d.id() == gid) {
            return &d;
        }
    }
    return nullptr;
}

Item* CalendarTodoWidget::find_any(const std::string& id) {
    for (Item& d : days_) {
        if (Item* hit = d.find(id)) {
            return hit;
        }
    }
    return nullptr;
}

const Item* CalendarTodoWidget::find_any(const std::string& id) const {
    for (const Item& d : days_) {
        if (const Item* hit = d.find(id)) {
            return hit;
        }
    }
    return nullptr;
}

TodoStatus CalendarTodoWidget::load(const std::string& text) {
    ParseResult pr = ds::format::parse(text);
    if (!pr.ok()) {
        return TodoStatus::ParseError;
    }
    const Document& doc = pr.document();

    Value days_value = Value::list({});
    if (const Value* found = doc.root.find(kDaysKey)) {
        // E7-12 已知限制（見其標頭）：空 List 序列化後再解析會塌成 Null。save() 對「整份森林
        // 皆空」的狀態即會產出這種輸出；此處視同缺省 / 空清單處理，而非型別錯誤，避免
        // save()->load() round-trip 在「空 widget」情境下無謂失敗。其餘非 list 型別（如
        // 呼叫端手寫 `days: 5`）仍是契約違反，交由下方 build_forest 的 !is_list() 檢查回 Invalid。
        if (!found->is_null()) {
            days_value = *found;
        }
    }

    ForestResult fr = ds::format::build_forest(days_value);
    if (!fr.ok()) {
        return TodoStatus::Invalid;
    }

    days_ = fr.items();
    return TodoStatus::Ok;
}

TodoStatus CalendarTodoWidget::add_item(const std::string& date, const std::string& item_id,
                                        const std::string& text) {
    if (date.empty() || item_id.empty()) {
        return TodoStatus::Invalid;
    }
    if (find_any(item_id) != nullptr) {
        return TodoStatus::DuplicateId;
    }

    Item* group = find_date_group(date);
    if (group == nullptr) {
        days_.push_back(Item(date_group_id(date), date, Value::null()));
        group = &days_.back();
    }

    Value payload = Value::map({{kDoneKey, Value::boolean(false)}});
    group->add_child(Item(item_id, text, std::move(payload)));
    return TodoStatus::Ok;
}

TodoStatus CalendarTodoWidget::toggle_done(const std::string& item_id) {
    if (is_date_group_id(item_id)) {
        return TodoStatus::Invalid;
    }
    Item* node = find_any(item_id);
    if (node == nullptr) {
        return TodoStatus::NotFound;
    }
    const bool current = read_done(node->value());
    node->set_value(Value::map({{kDoneKey, Value::boolean(!current)}}));
    return TodoStatus::Ok;
}

const std::vector<Item>& CalendarTodoWidget::list_items(const std::string& date) const {
    const Item* group = find_date_group(date);
    return group != nullptr ? group->children() : empty_items();
}

Value CalendarTodoWidget::to_value(const Item& item) {
    std::vector<Value::Member> members;
    members.emplace_back(std::string(ds::format::item_keys::kId), Value::string(item.id()));
    members.emplace_back(std::string(ds::format::item_keys::kLabel), Value::string(item.label()));
    if (!item.value().is_null()) {
        members.emplace_back(std::string(ds::format::item_keys::kValue), item.value());
    }
    if (!item.children().empty()) {
        std::vector<Value> kids;
        kids.reserve(item.children().size());
        for (const Item& c : item.children()) {
            kids.push_back(to_value(c));
        }
        members.emplace_back(std::string(ds::format::item_keys::kChildren),
                             Value::list(std::move(kids)));
    }
    return Value::map(std::move(members));
}

std::string CalendarTodoWidget::save() const {
    std::vector<Value> day_values;
    day_values.reserve(days_.size());
    for (const Item& d : days_) {
        day_values.push_back(to_value(d));
    }
    Value root = Value::map({{kDaysKey, Value::list(std::move(day_values))}});
    return ds::format::serialize(root, ds::format::kSupportedFormat);
}

ds::render::LayoutResult CalendarTodoWidget::render_model(
    const ds::render::LayoutConstraints& constraints) const {
    std::string text;
    bool first_line = true;
    for (const Item& d : days_) {
        if (!first_line) {
            text += '\n';
        }
        first_line = false;
        text += d.label().empty() ? d.id() : d.label();

        for (const Item& it : d.children()) {
            text += '\n';
            text += read_done(it.value()) ? "[x] " : "[ ] ";
            text += it.label();
        }
    }

    ds::render::TextLayout layout(metrics_, shell_.id());
    return layout.layout(text, constraints);
}

std::vector<std::string> CalendarTodoWidget::dates() const {
    std::vector<std::string> out;
    out.reserve(days_.size());
    for (const Item& d : days_) {
        out.push_back(d.label());
    }
    return out;
}

bool CalendarTodoWidget::has_date(const std::string& date) const {
    return find_date_group(date) != nullptr;
}

std::size_t CalendarTodoWidget::item_count() const noexcept {
    std::size_t n = 0;
    for (const Item& d : days_) {
        n += d.child_count();
    }
    return n;
}

bool CalendarTodoWidget::contains(const std::string& item_id) const {
    return find_any(item_id) != nullptr;
}

TodoStatus CalendarTodoWidget::is_done(const std::string& item_id, bool& out) const {
    if (is_date_group_id(item_id)) {
        return TodoStatus::Invalid;
    }
    const Item* node = find_any(item_id);
    if (node == nullptr) {
        return TodoStatus::NotFound;
    }
    out = read_done(node->value());
    return TodoStatus::Ok;
}

}  // namespace ds::widgets
