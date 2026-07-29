// E11-02 自繪選單呈現 — 實作。見 menu_renderer.hpp。
//
// 平台中立純邏輯：無 `#ifdef` / 系統呼叫 / 真實選單 API。消費 E7-13 Item 森林、
// 以 E4-01 排版標籤、以 E1-03 具名 SurfaceId 表達面板呈現目標。
#include "menu_renderer.hpp"

#include <algorithm>
#include <utility>

namespace ds::host {

// -----------------------------------------------------------------------------
// MenuNode
// -----------------------------------------------------------------------------

MenuNode::MenuNode(std::string id, std::string label, MenuNodeKind kind, bool enabled,
                   bool checked)
    : id_(std::move(id)),
      label_(std::move(label)),
      kind_(kind),
      enabled_(enabled),
      checked_(checked) {}

// -----------------------------------------------------------------------------
// MenuModel
// -----------------------------------------------------------------------------

MenuNode* MenuModel::at_path(const std::vector<std::size_t>& path) {
    if (path.empty()) return nullptr;
    std::vector<MenuNode>* level = &items_;
    MenuNode* current = nullptr;
    for (std::size_t depth = 0; depth < path.size(); ++depth) {
        const std::size_t idx = path[depth];
        if (level == nullptr || idx >= level->size()) return nullptr;
        current = &(*level)[idx];
        level = &current->children();  // 非子選單時為空，越界即回 nullptr。
    }
    return current;
}

const MenuNode* MenuModel::at_path(const std::vector<std::size_t>& path) const {
    return const_cast<MenuModel*>(this)->at_path(path);
}

// -----------------------------------------------------------------------------
// MenuBuildResult
// -----------------------------------------------------------------------------

MenuBuildResult MenuBuildResult::success(MenuModel model) {
    MenuBuildResult r;
    r.ok_ = true;
    r.model_ = std::move(model);
    return r;
}

MenuBuildResult MenuBuildResult::failure(MenuBuildError e) {
    MenuBuildResult r;
    r.ok_ = false;
    r.error_ = std::move(e);
    return r;
}

// -----------------------------------------------------------------------------
// build_menu — E7-13 Item 森林 → MenuModel（錯誤可定位、不靜默）
// -----------------------------------------------------------------------------

namespace {

bool parse_kind(const std::string& s, MenuNodeKind& out) {
    if (s == "action") {
        out = MenuNodeKind::Action;
        return true;
    }
    if (s == "separator") {
        out = MenuNodeKind::Separator;
        return true;
    }
    if (s == "checkbox") {
        out = MenuNodeKind::Checkbox;
        return true;
    }
    if (s == "submenu") {
        out = MenuNodeKind::Submenu;
        return true;
    }
    return false;
}

// 遞迴建構單一節點。ctx = 給人看的定位脈絡。
bool build_node(const ds::format::Item& item, const std::string& ctx, MenuNode& out,
                MenuBuildError& err) {
    const std::string tag = ctx + " (id '" + item.id() + "')";
    const ds::format::Value& val = item.value();

    if (!val.is_null() && !val.is_map()) {
        err.message = tag + ": value must be a map (kind/checked/enabled) or absent";
        return false;
    }

    // kind：省略時依是否帶子項推斷（有子項 → submenu，否則 → action）。
    MenuNodeKind kind = item.children().empty() ? MenuNodeKind::Action : MenuNodeKind::Submenu;
    if (val.is_map()) {
        if (const ds::format::Value* kv = val.find(menu_item_keys::kKind)) {
            if (!kv->is_string()) {
                err.message = tag + ": 'kind' must be a string";
                return false;
            }
            if (!parse_kind(kv->as_string(), kind)) {
                err.message = tag + ": unknown kind '" + kv->as_string() + "'";
                return false;
            }
        }
    }

    // 只有 submenu 可帶子項：其餘型別帶子項視為結構矛盾（不靜默忽略）。
    if (kind != MenuNodeKind::Submenu && !item.children().empty()) {
        err.message = tag + ": only submenu items may have children";
        return false;
    }

    bool enabled = true;
    if (val.is_map()) {
        if (const ds::format::Value* ev = val.find(menu_item_keys::kEnabled)) {
            if (!ev->is_bool()) {
                err.message = tag + ": 'enabled' must be a bool";
                return false;
            }
            enabled = ev->as_bool();
        }
    }

    bool checked = false;
    if (val.is_map()) {
        if (const ds::format::Value* cv = val.find(menu_item_keys::kChecked)) {
            if (kind != MenuNodeKind::Checkbox) {
                err.message = tag + ": 'checked' only valid for checkbox items";
                return false;
            }
            if (!cv->is_bool()) {
                err.message = tag + ": 'checked' must be a bool";
                return false;
            }
            checked = cv->as_bool();
        }
    }

    out = MenuNode(item.id(), item.label(), kind, enabled, checked);

    if (kind == MenuNodeKind::Submenu) {
        const std::vector<ds::format::Item>& kids = item.children();
        for (std::size_t i = 0; i < kids.size(); ++i) {
            MenuNode child;
            const std::string child_ctx = tag + " child #" + std::to_string(i);
            if (!build_node(kids[i], child_ctx, child, err)) {
                return false;
            }
            out.add_child(std::move(child));
        }
    }

    return true;
}

}  // namespace

MenuBuildResult build_menu(const std::vector<ds::format::Item>& forest) {
    std::vector<MenuNode> items;
    items.reserve(forest.size());
    for (std::size_t i = 0; i < forest.size(); ++i) {
        MenuNode node;
        MenuBuildError err;
        const std::string ctx = "menu item #" + std::to_string(i);
        if (!build_node(forest[i], ctx, node, err)) {
            return MenuBuildResult::failure(std::move(err));
        }
        items.push_back(std::move(node));
    }
    return MenuBuildResult::success(MenuModel(std::move(items)));
}

MenuBuildResult build_menu(const ds::format::Item& root) { return build_menu(root.children()); }

// -----------------------------------------------------------------------------
// CustomMenuRenderer
// -----------------------------------------------------------------------------

CustomMenuRenderer::CustomMenuRenderer(const ds::render::FontMetrics& metrics,
                                       ds::kernel::SurfaceId root_surface)
    : metrics_(metrics), text_layout_(metrics), root_surface_(std::move(root_surface)) {}

MenuBuildResult CustomMenuRenderer::set_menu(const std::vector<ds::format::Item>& forest) {
    MenuBuildResult result = build_menu(forest);
    if (result.ok()) {
        model_ = result.model();
        cursor_.clear();
    }
    return result;
}

namespace {

// 給定同層節點，取得所有可巡覽（可高亮停駐）項目的索引，保序。
std::vector<std::size_t> navigable_indices(const std::vector<MenuNode>& level) {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < level.size(); ++i) {
        if (level[i].is_navigable()) out.push_back(i);
    }
    return out;
}

}  // namespace

const std::vector<MenuNode>* CustomMenuRenderer::level_at(
    const std::vector<std::size_t>& parent_path) const {
    if (parent_path.empty()) return &model_.items();
    const MenuNode* node = model_.at_path(parent_path);
    if (node == nullptr || node->kind() != MenuNodeKind::Submenu) return nullptr;
    return &node->children();
}

bool CustomMenuRenderer::path_visible(const std::vector<std::size_t>& path) const {
    if (path.empty()) return false;
    if (path.size() == 1) return true;  // 根面板恆可見。
    if (cursor_.size() < path.size() - 1) return false;
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        if (cursor_[i] != path[i]) return false;
    }
    return true;
}

MenuNavResult CustomMenuRenderer::move_by(bool forward) {
    MenuNavResult out;
    if (model_.empty()) {
        out.status = MenuNavStatus::Empty;
        return out;
    }

    std::vector<std::size_t> parent_path = cursor_;
    if (!parent_path.empty()) parent_path.pop_back();

    const std::vector<MenuNode>* level = level_at(parent_path);
    if (level == nullptr) {
        out.status = MenuNavStatus::Invalid;
        return out;
    }

    const std::vector<std::size_t> nav = navigable_indices(*level);
    if (nav.empty()) {
        out.status = MenuNavStatus::Invalid;
        return out;
    }

    std::size_t next_idx;
    if (cursor_.empty()) {
        next_idx = forward ? nav.front() : nav.back();
    } else {
        const std::size_t cur = cursor_.back();
        const auto it = std::find(nav.begin(), nav.end(), cur);
        if (it == nav.end()) {
            // 目前高亮列已非可巡覽（如被停用）：回退取邊界項。
            next_idx = forward ? nav.front() : nav.back();
        } else {
            std::size_t pos = static_cast<std::size_t>(it - nav.begin());
            if (forward) {
                pos = (pos + 1) % nav.size();
            } else {
                pos = (pos == 0) ? nav.size() - 1 : pos - 1;
            }
            next_idx = nav[pos];
        }
    }

    parent_path.push_back(next_idx);
    cursor_ = std::move(parent_path);
    out.status = MenuNavStatus::Moved;
    out.path = cursor_;
    return out;
}

MenuNavResult CustomMenuRenderer::move_next() { return move_by(true); }
MenuNavResult CustomMenuRenderer::move_prev() { return move_by(false); }

MenuNavResult CustomMenuRenderer::enter() {
    MenuNavResult out;
    if (cursor_.empty()) {
        out.status = MenuNavStatus::Invalid;
        return out;
    }
    const MenuNode* node = model_.at_path(cursor_);
    if (node == nullptr) {
        out.status = MenuNavStatus::Invalid;
        return out;
    }
    if (node->kind() != MenuNodeKind::Submenu || !node->enabled()) {
        out.status = MenuNavStatus::NotSelectable;
        return out;
    }
    const std::vector<std::size_t> nav = navigable_indices(node->children());
    if (nav.empty()) {
        out.status = MenuNavStatus::NotSelectable;  // 空子選單：無可進入項。
        return out;
    }
    cursor_.push_back(nav.front());
    out.status = MenuNavStatus::Entered;
    out.path = cursor_;
    return out;
}

MenuNavResult CustomMenuRenderer::exit() {
    MenuNavResult out;
    if (cursor_.size() <= 1) {
        out.status = MenuNavStatus::Invalid;  // 已在根層，無可收合的面板。
        return out;
    }
    cursor_.pop_back();
    out.status = MenuNavStatus::Exited;
    out.path = cursor_;
    return out;
}

MenuNavResult CustomMenuRenderer::activate() {
    MenuNavResult out;
    if (cursor_.empty()) {
        out.status = MenuNavStatus::Invalid;
        return out;
    }
    MenuNode* node = model_.at_path(cursor_);
    if (node == nullptr) {
        out.status = MenuNavStatus::Invalid;
        return out;
    }

    if (node->kind() == MenuNodeKind::Submenu) {
        return enter();
    }
    if (!node->is_activatable()) {
        out.status = MenuNavStatus::NotSelectable;  // 分隔線 / 停用項。
        return out;
    }
    if (node->kind() == MenuNodeKind::Checkbox) {
        out.checked = node->toggle_checked();
    }
    out.status = MenuNavStatus::Selected;
    out.path = cursor_;
    return out;
}

MenuNavResult CustomMenuRenderer::hover(const std::vector<std::size_t>& path) {
    MenuNavResult out;
    if (!path_visible(path)) {
        out.status = MenuNavStatus::Invalid;
        return out;
    }
    const MenuNode* node = model_.at_path(path);
    if (node == nullptr) {
        out.status = MenuNavStatus::Invalid;
        return out;
    }
    if (!node->is_navigable()) {
        out.status = MenuNavStatus::NotSelectable;  // 分隔線 / 停用項：不變更游標。
        return out;
    }
    cursor_ = path;
    out.status = MenuNavStatus::Moved;
    out.path = cursor_;
    return out;
}

MenuNavResult CustomMenuRenderer::select(const std::vector<std::size_t>& path) {
    const MenuNavResult hovered = hover(path);
    if (hovered.status != MenuNavStatus::Moved) {
        return hovered;  // Invalid / NotSelectable：直接回報，不致動。
    }
    return activate();
}

ds::kernel::SurfaceId CustomMenuRenderer::panel_surface_name(
    const std::vector<std::size_t>& parent_path) const {
    if (parent_path.empty()) return root_surface_;
    const MenuNode* owner = model_.at_path(parent_path);
    const std::string owner_id = (owner != nullptr) ? owner->id() : std::string("unknown");
    // 具名尾碼取自擁有者子選單項的具名 id，非數字深度 / z-order（NFR-02）。
    return root_surface_ + ".menu:" + owner_id;
}

MenuRenderModel CustomMenuRenderer::render_model() const {
    MenuRenderModel out;
    if (model_.empty()) return out;

    const std::size_t panel_count = cursor_.empty() ? 1 : cursor_.size();
    std::vector<std::size_t> parent_path;

    for (std::size_t depth = 0; depth < panel_count; ++depth) {
        const std::vector<MenuNode>* level = level_at(parent_path);
        if (level == nullptr) break;  // 防禦：游標與模型不同步時安全中止（不崩潰）。

        MenuPanelRender panel;
        panel.owner_path = parent_path;
        panel.surface = panel_surface_name(parent_path);

        double y = 0.0;
        double max_width = 0.0;
        for (std::size_t i = 0; i < level->size(); ++i) {
            const MenuNode& node = (*level)[i];

            MenuRowRender row;
            row.path = parent_path;
            row.path.push_back(i);
            row.kind = node.kind();
            row.enabled = node.enabled();
            row.checked = node.checked();
            row.has_submenu = node.is_submenu();
            row.selected = (depth < cursor_.size() && cursor_[depth] == i);
            row.y = y;

            if (node.is_separator()) {
                row.row_height = metrics_.line_height() * 0.5;
                row.label.surface = panel.surface;
            } else {
                row.label = text_layout_.layout(node.label(), constraints_);
                row.label.surface = panel.surface;
                row.row_height =
                    row.label.size.height > 0.0 ? row.label.size.height : metrics_.line_height();
                max_width = std::max(max_width, row.label.size.width);
            }

            y += row.row_height;
            panel.rows.push_back(std::move(row));
        }

        panel.size.width = max_width;
        panel.size.height = y;
        out.panels.push_back(std::move(panel));

        if (depth < cursor_.size()) parent_path.push_back(cursor_[depth]);
    }

    return out;
}

}  // namespace ds::host
