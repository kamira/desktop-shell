// content/c3_01/launcher_menu_tree.cpp — C3-01 啟動器選單樹實作
#include "launcher_menu_tree.hpp"

#include <utility>

namespace ds::content {

const char* to_string(SelectOutcome r) noexcept {
    switch (r) {
        case SelectOutcome::Activated:
            return "Activated";
        case SelectOutcome::ActivationFailed:
            return "ActivationFailed";
        case SelectOutcome::NotLeaf:
            return "NotLeaf";
        case SelectOutcome::NotFound:
            return "NotFound";
    }
    return "unknown";
}

bool LauncherMenuTree::load_menu(const ds::format::Value& declarative_list) {
    if (!base_.set_items(declarative_list)) {
        return false;  // 結構違反：現有選單不動，展開狀態也不動。
    }
    expanded_.clear();
    return true;
}

void LauncherMenuTree::load_menu(std::vector<ds::format::Item> items) {
    base_.set_items(std::move(items));
    expanded_.clear();
}

const ds::format::Item* LauncherMenuTree::find_node(const std::string& id) const {
    for (const auto& root : base_.items()) {
        if (const ds::format::Item* found = root.find(id)) {
            return found;
        }
    }
    return nullptr;
}

bool LauncherMenuTree::expand(const std::string& id) {
    const ds::format::Item* node = find_node(id);
    if (node == nullptr || node->is_leaf()) {
        return false;  // 不存在，或葉節點（無子項目，展開無意義）。
    }
    return expanded_.insert(id).second;  // 已展開中 -> false（不靜默重複展開）。
}

bool LauncherMenuTree::collapse(const std::string& id) {
    const ds::format::Item* node = find_node(id);
    if (node == nullptr || node->is_leaf()) {
        return false;
    }
    return expanded_.erase(id) > 0;  // 尚未展開 -> false（no-op）。
}

bool LauncherMenuTree::is_expanded(const std::string& id) const noexcept {
    return expanded_.find(id) != expanded_.end();
}

bool LauncherMenuTree::parse_leaf_action(const ds::format::Item& leaf,
                                         ds::command::CommandId& out_id,
                                         ds::command::CommandArgs& out_args,
                                         std::string& out_error) const {
    const ds::format::Value& v = leaf.value();
    if (!v.is_map()) {
        out_error = "item '" + leaf.id() + "': value must be a map with '" +
                    std::string(menu_keys::kCommand) + "'";
        return false;
    }
    const ds::format::Value* cmd = v.find(menu_keys::kCommand);
    if (cmd == nullptr || !cmd->is_string() || cmd->as_string().empty()) {
        out_error = "item '" + leaf.id() + "': missing/invalid '" +
                    std::string(menu_keys::kCommand) + "'";
        return false;
    }
    out_id = cmd->as_string();

    if (const ds::format::Value* args = v.find(menu_keys::kArgs)) {
        if (!args->is_map()) {
            out_error = "item '" + leaf.id() + "': '" + std::string(menu_keys::kArgs) +
                        "' must be a map";
            return false;
        }
        for (const auto& member : args->as_map()) {
            if (!member.second.is_string()) {
                out_error = "item '" + leaf.id() + "': args['" + member.first +
                            "'] must be a string";
                return false;
            }
            out_args.set(member.first, member.second.as_string());
        }
    }
    return true;
}

SelectOutcome LauncherMenuTree::select(const std::string& id) {
    const ds::format::Item* node = find_node(id);
    if (node == nullptr) {
        return SelectOutcome::NotFound;
    }
    if (!node->is_leaf()) {
        return SelectOutcome::NotLeaf;  // 非葉節點：不啟動；呼叫端應改用 expand()/collapse()。
    }
    return activate(id);
}

SelectOutcome LauncherMenuTree::activate(const std::string& id) {
    const ds::format::Item* node = find_node(id);
    if (node == nullptr) {
        return SelectOutcome::NotFound;
    }
    if (!node->is_leaf()) {
        return SelectOutcome::NotLeaf;
    }

    ds::command::CommandId command_id;
    ds::command::CommandArgs args;
    std::string error;
    if (!parse_leaf_action(*node, command_id, args, error)) {
        last_command_result_ = ds::command::CommandResult::make_failed(error);
        return SelectOutcome::ActivationFailed;
    }

    last_command_result_ = bus_.dispatch(command_id, args);
    return last_command_result_.ok() ? SelectOutcome::Activated : SelectOutcome::ActivationFailed;
}

ds::format::Value make_launch_program_value(std::string program, std::string args) {
    std::vector<ds::format::Value::Member> arg_members;
    arg_members.emplace_back("program", ds::format::Value::string(std::move(program)));
    if (!args.empty()) {
        arg_members.emplace_back("args", ds::format::Value::string(std::move(args)));
    }
    std::vector<ds::format::Value::Member> members;
    members.emplace_back(menu_keys::kCommand,
                         ds::format::Value::string(ds::actuators::kCmdLaunchProgram));
    members.emplace_back(menu_keys::kArgs, ds::format::Value::map(std::move(arg_members)));
    return ds::format::Value::map(std::move(members));
}

ds::format::Value make_open_file_value(std::string path) {
    std::vector<ds::format::Value::Member> arg_members;
    arg_members.emplace_back("path", ds::format::Value::string(std::move(path)));
    std::vector<ds::format::Value::Member> members;
    members.emplace_back(menu_keys::kCommand,
                         ds::format::Value::string(ds::actuators::kCmdOpenFile));
    members.emplace_back(menu_keys::kArgs, ds::format::Value::map(std::move(arg_members)));
    return ds::format::Value::map(std::move(members));
}

ds::format::Value make_web_search_value(std::string query, std::string engine) {
    std::vector<ds::format::Value::Member> arg_members;
    arg_members.emplace_back("query", ds::format::Value::string(std::move(query)));
    if (!engine.empty()) {
        arg_members.emplace_back("engine", ds::format::Value::string(std::move(engine)));
    }
    std::vector<ds::format::Value::Member> members;
    members.emplace_back(menu_keys::kCommand,
                         ds::format::Value::string(ds::actuators::kCmdWebSearch));
    members.emplace_back(menu_keys::kArgs, ds::format::Value::map(std::move(arg_members)));
    return ds::format::Value::map(std::move(members));
}

}  // namespace ds::content
