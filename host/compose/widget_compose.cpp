// H1-07 組件組合裝配 —— 實作。平台中立、純邏輯（見 `widget_compose.hpp` 檔頭）。
#include "widget_compose.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ds::host {
namespace {

// int 值域夾限：縮放後的像素值可能超出 int（惡意 / 極端的縮放係數），夾限而非溢位（UB）。
int clamp_to_int(long long v) noexcept {
    constexpr long long kMin = static_cast<long long>(std::numeric_limits<int>::min());
    constexpr long long kMax = static_cast<long long>(std::numeric_limits<int>::max());
    if (v < kMin) return std::numeric_limits<int>::min();
    if (v > kMax) return std::numeric_limits<int>::max();
    return static_cast<int>(v);
}

}  // namespace

// ---------------------------------------------------------------------------
// 像素點 / 尺寸
// ---------------------------------------------------------------------------
bool operator==(const PixelPoint& a, const PixelPoint& b) noexcept {
    return a.x == b.x && a.y == b.y;
}
bool operator!=(const PixelPoint& a, const PixelPoint& b) noexcept { return !(a == b); }

bool operator==(const PixelSize& a, const PixelSize& b) noexcept {
    return a.width == b.width && a.height == b.height;
}
bool operator!=(const PixelSize& a, const PixelSize& b) noexcept { return !(a == b); }

// ---------------------------------------------------------------------------
// DPI 縮放
// ---------------------------------------------------------------------------
double sane_scale(double scale) noexcept {
    // `!(scale > 0.0)` 而非 `scale <= 0.0`：NaN 的任何比較都為 false，後者對 NaN 不成立，
    // NaN 會落到「係數正常」的分支（K-002 的形狀：壞掉的值偽裝成一個看起來正常的結果）。
    if (!(scale > 0.0) || !std::isfinite(scale)) return ds::kernel::kDefaultScaleFactor;
    return scale;
}

int scale_logical(int logical, double scale) noexcept {
    const double s = sane_scale(scale);
    double scaled = static_cast<double>(logical) * s;
    if (!std::isfinite(scaled)) return 0;  // 不可能走到（s 有限、logical 有限），但不留 UB 的路
    // `std::llround` 的引數超出 long long 值域時，回傳值未指定且會升起 FE_INVALID。
    // 極大的縮放係數（如 1e300）配上非零的邏輯像素就會走到那裡，所以**先在 double 的
    // 值域內夾限，再 llround**。夾限門檻取遠小於 long long 上限的 1e18。
    constexpr double kRoundLimit = 1e18;
    if (scaled > kRoundLimit) scaled = kRoundLimit;
    if (scaled < -kRoundLimit) scaled = -kRoundLimit;
    return clamp_to_int(std::llround(scaled));
}

PaintBox scale_box(PixelPoint origin, PixelSize size, double scale) noexcept {
    const double s = sane_scale(scale);
    PaintBox box;
    box.left = scale_logical(origin.x, s);
    box.top = scale_logical(origin.y, s);
    // 右 / 下緣由**邏輯座標的和**縮放而來，而不是 left + scale(width)：後者會讓相鄰組件
    // 在非整數縮放下出現 1px 的縫或重疊（12*1.5=18、(12+100)*1.5=168，而 18+150=168 ✓；
    // 若改用 left+scale(width)，相鄰組件的邊界就不保證對齊）。
    box.right = scale_logical(clamp_to_int(static_cast<long long>(origin.x) + size.width), s);
    box.bottom = scale_logical(clamp_to_int(static_cast<long long>(origin.y) + size.height), s);
    return box;
}

// ---------------------------------------------------------------------------
// 呈現型態
// ---------------------------------------------------------------------------
double ComponentPresentation::param(const std::string& name, double fallback) const {
    const auto it = params.find(name);
    return it == params.end() ? fallback : it->second;
}

bool operator==(const ComponentPresentation& a, const ComponentPresentation& b) {
    return a.variant == b.variant && a.visible == b.visible && a.params == b.params;
}
bool operator!=(const ComponentPresentation& a, const ComponentPresentation& b) {
    return !(a == b);
}

// ---------------------------------------------------------------------------
// 具名結果
// ---------------------------------------------------------------------------
const char* to_string(ComposeStatus status) noexcept {
    switch (status) {
        case ComposeStatus::Ok: return "Ok";
        case ComposeStatus::EmptySlotId: return "EmptySlotId";
        case ComposeStatus::DuplicateSlot: return "DuplicateSlot";
        case ComposeStatus::UnknownSlot: return "UnknownSlot";
        case ComposeStatus::DegenerateSize: return "DegenerateSize";
        case ComposeStatus::OutOfBounds: return "OutOfBounds";
        case ComposeStatus::SlotAlreadyBound: return "SlotAlreadyBound";
        case ComposeStatus::SlotNotBound: return "SlotNotBound";
        case ComposeStatus::Incompatible: return "Incompatible";
        case ComposeStatus::EmptyWidget: return "EmptyWidget";
        case ComposeStatus::IncompleteComposition: return "IncompleteComposition";
    }
    return "Unknown";  // 列舉被擴充而忘了補上面 —— 回具名字串，不回空指標
}

ComposeResult ComposeResult::success() { return ComposeResult{ComposeStatus::Ok, {}}; }

ComposeResult ComposeResult::failure(ComposeStatus status, std::string why) {
    return ComposeResult{status, std::move(why)};
}

// ---------------------------------------------------------------------------
// WidgetLayout
// ---------------------------------------------------------------------------
const ResolvedComponent* WidgetLayout::find(const std::string& slot_id) const noexcept {
    for (const auto& c : components) {
        if (c.slot_id == slot_id) return &c;
    }
    return nullptr;
}

bool boxes_overlap(const PaintBox& a, const PaintBox& b) noexcept {
    if (a.empty() || b.empty()) return false;  // 沒有面積的框不與任何東西重疊
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

// ---------------------------------------------------------------------------
// WidgetAssembly
// ---------------------------------------------------------------------------
WidgetAssembly::WidgetAssembly(std::string widget_id, PixelSize logical_size)
    : widget_id_(std::move(widget_id)), logical_size_(logical_size) {}

std::size_t WidgetAssembly::index_of(const std::string& slot_id) const noexcept {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].id == slot_id) return i;
    }
    return static_cast<std::size_t>(-1);
}

ComposeResult WidgetAssembly::check_placement(const ComponentPlacement& placement) const {
    if (logical_size_.empty()) {
        return ComposeResult::failure(ComposeStatus::DegenerateSize,
                                      "widget '" + widget_id_ + "' 的邏輯尺寸非正（寬 " +
                                          std::to_string(logical_size_.width) + "、高 " +
                                          std::to_string(logical_size_.height) + "）");
    }
    if (placement.size.empty()) {
        return ComposeResult::failure(ComposeStatus::DegenerateSize,
                                      "組件尺寸非正（寬 " + std::to_string(placement.size.width) +
                                          "、高 " + std::to_string(placement.size.height) + "）");
    }
    if (placement.origin.x < 0 || placement.origin.y < 0) {
        return ComposeResult::failure(
            ComposeStatus::OutOfBounds,
            "組件原點為負（" + std::to_string(placement.origin.x) + "," +
                std::to_string(placement.origin.y) + "）—— widget 內相對座標的原點是左上角 (0,0)");
    }
    const long long right = static_cast<long long>(placement.origin.x) + placement.size.width;
    const long long bottom = static_cast<long long>(placement.origin.y) + placement.size.height;
    if (right > logical_size_.width || bottom > logical_size_.height) {
        return ComposeResult::failure(
            ComposeStatus::OutOfBounds,
            "組件超出 widget 邏輯邊界：右下 (" + std::to_string(right) + "," +
                std::to_string(bottom) + ") 超過 (" + std::to_string(logical_size_.width) + "," +
                std::to_string(logical_size_.height) + ")");
    }
    return ComposeResult::success();
}

ComposeResult WidgetAssembly::declare(const ds::package::ComponentSlot& slot,
                                      const ComponentPlacement& placement) {
    if (slot.id.empty()) {
        return ComposeResult::failure(ComposeStatus::EmptySlotId, "插槽 id 不得為空");
    }
    if (has_slot(slot.id)) {
        return ComposeResult::failure(ComposeStatus::DuplicateSlot,
                                      "widget '" + widget_id_ + "' 已有插槽 '" + slot.id + "'");
    }
    // **先驗放置、後動組合**：放置不合法時 E9-03 的組合物件完全沒被碰過（不部分套用）。
    const ComposeResult fits = check_placement(placement);
    if (!fits) return fits;

    const ds::package::BindResult added = composition_.add_slot(slot);
    if (!added) {
        // E9-03 自己的判定（id 空 / 重複）。上面兩道已先擋掉，走到這裡表示兩層的判定分歧 ——
        // 原因原樣傳出，不自己編一個。
        return ComposeResult::failure(ComposeStatus::DuplicateSlot, added.message);
    }
    slots_.push_back(SlotPlacement{slot.id, placement});
    return ComposeResult::success();
}

ComposeResult WidgetAssembly::bind(const std::string& slot_id,
                                   const ds::package::Package& component) {
    if (!has_slot(slot_id)) {
        return ComposeResult::failure(ComposeStatus::UnknownSlot,
                                      "widget '" + widget_id_ + "' 沒有插槽 '" + slot_id + "'");
    }
    if (composition_.is_bound(slot_id)) {
        return ComposeResult::failure(ComposeStatus::SlotAlreadyBound,
                                      "插槽 '" + slot_id + "' 已綁定，替換請用 swap()");
    }
    const ds::package::BindResult r = composition_.bind(slot_id, component);
    if (!r) {
        // 前兩道已排除「插槽不存在」與「已綁定」，E9-03 剩下的唯一失敗原因是不相容。
        return ComposeResult::failure(ComposeStatus::Incompatible, r.message);
    }
    return ComposeResult::success();
}

ComposeResult WidgetAssembly::swap(const std::string& slot_id,
                                   const ds::package::Package& component) {
    if (!has_slot(slot_id)) {
        return ComposeResult::failure(ComposeStatus::UnknownSlot,
                                      "widget '" + widget_id_ + "' 沒有插槽 '" + slot_id + "'");
    }
    if (!composition_.is_bound(slot_id)) {
        return ComposeResult::failure(ComposeStatus::SlotNotBound,
                                      "插槽 '" + slot_id + "' 尚未綁定，首次綁定請用 bind()");
    }
    const ds::package::BindResult r = composition_.swap(slot_id, component);
    if (!r) return ComposeResult::failure(ComposeStatus::Incompatible, r.message);
    return ComposeResult::success();
}

ComposeResult WidgetAssembly::unbind(const std::string& slot_id) {
    if (!has_slot(slot_id)) {
        return ComposeResult::failure(ComposeStatus::UnknownSlot,
                                      "widget '" + widget_id_ + "' 沒有插槽 '" + slot_id + "'");
    }
    if (!composition_.is_bound(slot_id)) {
        return ComposeResult::failure(ComposeStatus::SlotNotBound,
                                      "插槽 '" + slot_id + "' 本來就是空的");
    }
    const ds::package::BindResult r = composition_.unbind(slot_id);
    if (!r) return ComposeResult::failure(ComposeStatus::SlotNotBound, r.message);
    return ComposeResult::success();
}

ComposeResult WidgetAssembly::place(const std::string& slot_id, PixelPoint origin, PixelSize size) {
    const std::size_t i = index_of(slot_id);
    if (i == static_cast<std::size_t>(-1)) {
        return ComposeResult::failure(ComposeStatus::UnknownSlot,
                                      "widget '" + widget_id_ + "' 沒有插槽 '" + slot_id + "'");
    }
    ComponentPlacement next = slots_[i].placement;
    next.origin = origin;
    next.size = size;
    const ComposeResult fits = check_placement(next);
    if (!fits) return fits;  // 不合法 → 舊版面原封不動
    slots_[i].placement = next;
    return ComposeResult::success();
}

ComposeResult WidgetAssembly::present(const std::string& slot_id,
                                      const ComponentPresentation& presentation) {
    const std::size_t i = index_of(slot_id);
    if (i == static_cast<std::size_t>(-1)) {
        return ComposeResult::failure(ComposeStatus::UnknownSlot,
                                      "widget '" + widget_id_ + "' 沒有插槽 '" + slot_id + "'");
    }
    slots_[i].placement.presentation = presentation;
    return ComposeResult::success();
}

bool WidgetAssembly::has_slot(const std::string& slot_id) const noexcept {
    return index_of(slot_id) != static_cast<std::size_t>(-1);
}

bool WidgetAssembly::is_bound(const std::string& slot_id) const noexcept {
    return composition_.is_bound(slot_id);
}

const ds::package::Package* WidgetAssembly::bound_component(
    const std::string& slot_id) const noexcept {
    return composition_.bound_component(slot_id);
}

const ComponentPlacement* WidgetAssembly::placement(const std::string& slot_id) const noexcept {
    const std::size_t i = index_of(slot_id);
    return i == static_cast<std::size_t>(-1) ? nullptr : &slots_[i].placement;
}

std::vector<std::string> WidgetAssembly::slot_ids() const {
    std::vector<std::string> ids;
    ids.reserve(slots_.size());
    for (const auto& s : slots_) ids.push_back(s.id);
    return ids;
}

ComposeResult WidgetAssembly::resolve(double scale_factor, WidgetLayout& out) const {
    if (slots_.empty()) {
        return ComposeResult::failure(
            ComposeStatus::EmptyWidget,
            "widget '" + widget_id_ + "' 沒有任何組件 —— widget = 一或多個組件");
    }
    if (logical_size_.empty()) {
        return ComposeResult::failure(ComposeStatus::DegenerateSize,
                                      "widget '" + widget_id_ + "' 的邏輯尺寸非正");
    }
    // 組合完整性一律交給 E9-03：未綁定的插槽 / 已不相容的綁定都由它指名。
    const ds::package::BindResult valid = composition_.validate();
    if (!valid) {
        return ComposeResult::failure(ComposeStatus::IncompleteComposition, valid.message);
    }

    const double s = sane_scale(scale_factor);
    WidgetLayout layout;
    layout.widget_id = widget_id_;
    layout.scale_factor = s;
    layout.bounds = scale_box(PixelPoint{0, 0}, logical_size_, s);
    layout.components.reserve(slots_.size());
    for (const auto& slot : slots_) {
        const ds::package::Package* pkg = composition_.bound_component(slot.id);
        if (pkg == nullptr) {
            // validate() 已保證每個插槽都綁定；走到這裡表示 E9-03 的兩個查詢互相矛盾。
            return ComposeResult::failure(ComposeStatus::IncompleteComposition,
                                          "插槽 '" + slot.id + "' 通過 validate() 卻取不到綁定組件");
        }
        ResolvedComponent rc;
        rc.slot_id = slot.id;
        rc.component_name = pkg->manifest.name;
        rc.box = scale_box(slot.placement.origin, slot.placement.size, s);
        rc.presentation = slot.placement.presentation;
        rc.scale_factor = s;
        layout.components.push_back(std::move(rc));
    }
    out = std::move(layout);  // **全部算完才寫出**：失敗時呼叫端的 out 不會變成半套版面
    return ComposeResult::success();
}

ComposeResult WidgetAssembly::resolve(const ds::kernel::DpiInfo& dpi,
                                      const ds::kernel::ScreenId& screen,
                                      WidgetLayout& out) const {
    // kernel 交出來的是**具名螢幕的縮放係數（純比值）**，不是像素 —— 反向斷言要保住的形狀。
    return resolve(dpi.scale_of(screen), out);
}

// ---------------------------------------------------------------------------
// 跨 widget
// ---------------------------------------------------------------------------
std::vector<ComponentAppearance> appearances_of(const std::string& component_name,
                                                const std::vector<const WidgetAssembly*>& widgets,
                                                double scale_factor) {
    std::vector<ComponentAppearance> found;
    for (const WidgetAssembly* w : widgets) {
        if (w == nullptr) continue;
        WidgetLayout layout;
        if (!w->resolve(scale_factor, layout)) continue;  // 失敗由該 widget 自己回報
        for (const auto& c : layout.components) {
            if (c.component_name != component_name) continue;
            found.push_back(
                ComponentAppearance{layout.widget_id, c.slot_id, c.box, c.presentation});
        }
    }
    return found;
}

bool presentation_differs(const ComponentAppearance& a, const ComponentAppearance& b) {
    return a.presentation != b.presentation || a.box != b.box;
}

}  // namespace ds::host
