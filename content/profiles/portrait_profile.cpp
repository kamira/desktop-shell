// content/profiles/c1_02/portrait_profile.cpp — C1-02 立繪 profile 實作（組裝型 artifact 單元）
//
// 相位 1：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 #ifdef / win32 / cocoa）、無絕對座標 /
// 數字 z-order（NFR-02）。宣告式定義（E7-01）解讀後跨 E1-03 / E1-01 / E1-08 / E4-06（+E4-02）/
// E5-14 實體化；per-pixel alpha 能力經 has() 閘控（NFR-03）；無效輸入結構化回報、中途失敗
// 回滾，不靜默。
#include "portrait_profile.hpp"

#include <cmath>    // std::isfinite
#include <cstdint>  // std::int64_t
#include <utility>  // std::move

namespace ds::profiles {

namespace {

// 宣告式立繪定義的欄位名（NFR-02：位置 / 尺寸皆以正規化偏移或元件固有尺寸承載，非畫面
// 絕對像素座標）。
constexpr const char* kKeyLayer = "layer";
constexpr const char* kKeyAlpha = "alpha";
constexpr const char* kKeyAlphaMode = "mode";
constexpr const char* kKeyAlphaOpacity = "opacity";
constexpr const char* kKeyPosition = "position";
constexpr const char* kKeyPosAnchor = "anchor";
constexpr const char* kKeyPosDx = "dx";
constexpr const char* kKeyPosDy = "dy";
constexpr const char* kKeyExpressions = "expressions";
constexpr const char* kKeyExprName = "name";
constexpr const char* kKeyExprSource = "source";
constexpr const char* kKeyExprWidth = "width";
constexpr const char* kKeyExprHeight = "height";
constexpr const char* kKeyExprScale = "scale";
constexpr const char* kKeyInitialExpression = "initial_expression";

// 具名圖層字串 → E1-01 SurfaceLayer（NFR-02：具名，非數字層級）。未知回 false（不靜默）。
bool parse_layer(const std::string& name, ds::kernel::SurfaceLayer& out) {
    if (name == "wallpaper") {
        out = ds::kernel::SurfaceLayer::Wallpaper;
    } else if (name == "below-normal") {
        out = ds::kernel::SurfaceLayer::BelowNormal;
    } else if (name == "normal") {
        out = ds::kernel::SurfaceLayer::Normal;
    } else if (name == "overlay") {
        out = ds::kernel::SurfaceLayer::Overlay;
    } else if (name == "topmost") {
        out = ds::kernel::SurfaceLayer::Topmost;
    } else {
        return false;
    }
    return true;
}

// 具名 alpha 合成模式字串 → E1-03 AlphaMode。未知回 false（不靜默）。
bool parse_alpha_mode(const std::string& name, ds::kernel::AlphaMode& out) {
    if (name == "opaque") {
        out = ds::kernel::AlphaMode::Opaque;
    } else if (name == "per-pixel") {
        out = ds::kernel::AlphaMode::PerPixel;
    } else {
        return false;
    }
    return true;
}

// 具名縮放模式字串 → E4-02 ScaleMode。未知回 false（不靜默）。
bool parse_scale_mode(const std::string& name, ds::elements::ScaleMode& out) {
    if (name == "fill") {
        out = ds::elements::ScaleMode::Fill;
    } else if (name == "fit") {
        out = ds::elements::ScaleMode::Fit;
    } else if (name == "stretch") {
        out = ds::elements::ScaleMode::Stretch;
    } else if (name == "center") {
        out = ds::elements::ScaleMode::Center;
    } else if (name == "tile") {
        out = ds::elements::ScaleMode::Tile;
    } else {
        return false;
    }
    return true;
}

}  // namespace

const char* to_string(PortraitState s) noexcept {
    switch (s) {
        case PortraitState::Unloaded:
            return "Unloaded";
        case PortraitState::Loaded:
            return "Loaded";
    }
    return "unknown";
}

const char* to_string(PortraitStatus s) noexcept {
    switch (s) {
        case PortraitStatus::Ok:
            return "Ok";
        case PortraitStatus::Unsupported:
            return "Unsupported";
        case PortraitStatus::Invalid:
            return "Invalid";
        case PortraitStatus::AlreadyLoaded:
            return "AlreadyLoaded";
    }
    return "unknown";
}

PortraitProfile::PortraitProfile(std::string id, ds::kernel::KernelBackend& backend,
                                 ds::kernel::LayerStack& layers)
    : id_(std::move(id)), backend_(backend), layers_(layers), alpha_svc_(backend_), drag_(backend_) {}

PortraitProfile::~PortraitProfile() {
    if (state_ == PortraitState::Loaded) {
        unload();
    }
}

void PortraitProfile::teardown_backend_state() {
    // 反向拆除（各步對未知 id / 未知名皆安全 no-op）：
    // 區域登記 → 命中 surface → 表情登錄（E4-06 + E4-02）→ 位置登錄 → 圖層指派 → alpha surface。
    regions_.remove_regions(id_);
    regions_.set_surfaces({});
    for (const auto& name : switcher_.list()) {
        switcher_.unregister_surface(name);
    }
    expressions_.clear();
    drag_.forget(id_);
    layers_.remove(id_);
    alpha_svc_.destroy_alpha_surface(id_);
}

void PortraitProfile::refresh_hit_surface() {
    // 本立繪唯一參與命中測試的 surface：以目前表情（若有）的固有尺寸為形狀，否則退回 1x1
    // 最小佔位形狀。形狀 width/height 為元件本地固有尺寸（NFR-02：非畫面絕對座標）。
    ds::kernel::HitSurface hs;
    hs.id = id_;
    hs.layer = layer_;
    hs.hit = ds::kernel::HitPolicy::Solid;
    hs.alpha = alpha_;
    const ds::elements::ImageElement* img = current_image();
    if (img != nullptr && img->has_source()) {
        const ds::elements::ImageDimensions dims = img->source_dimensions();
        hs.shape = ds::kernel::make_rect(static_cast<float>(dims.width), static_cast<float>(dims.height));
    } else {
        hs.shape = ds::kernel::make_rect(1.0f, 1.0f);
    }
    regions_.set_surfaces({hs});
}

ds::elements::ImageElement* PortraitProfile::find_expression(const std::string& name) {
    for (auto& kv : expressions_) {
        if (kv.first == name) {
            return &kv.second;
        }
    }
    return nullptr;
}

const ds::elements::ImageElement* PortraitProfile::find_expression(const std::string& name) const {
    for (const auto& kv : expressions_) {
        if (kv.first == name) {
            return &kv.second;
        }
    }
    return nullptr;
}

PortraitStatus PortraitProfile::load_portrait(const ds::format::Value& definition) {
    if (state_ == PortraitState::Loaded) {
        return PortraitStatus::AlreadyLoaded;  // 不靜默重載；呼叫端須先 unload()。
    }
    if (id_.empty()) {
        return PortraitStatus::Invalid;
    }
    if (!definition.is_map()) {
        return PortraitStatus::Invalid;  // 宣告式定義須為 Map（通常為 Document::root）。
    }

    // --- 解讀宣告式定義為期望狀態（暫存；全數驗證通過且實體化成功才提交至成員）---
    ds::kernel::SurfaceLayer layer_new = layer_;
    ds::kernel::AlphaProfile alpha_new = alpha_;
    ds::kernel::AnchorSpec position_new = position_;

    if (const ds::format::Value* v = definition.find(kKeyLayer)) {
        if (!v->is_string() || !parse_layer(v->as_string(), layer_new)) {
            return PortraitStatus::Invalid;
        }
    }
    if (const ds::format::Value* v = definition.find(kKeyAlpha)) {
        if (!v->is_map()) {
            return PortraitStatus::Invalid;
        }
        if (const ds::format::Value* mode_v = v->find(kKeyAlphaMode)) {
            if (!mode_v->is_string() || !parse_alpha_mode(mode_v->as_string(), alpha_new.mode)) {
                return PortraitStatus::Invalid;
            }
        }
        if (const ds::format::Value* op_v = v->find(kKeyAlphaOpacity)) {
            if (!op_v->is_number() || !std::isfinite(op_v->as_number())) {
                return PortraitStatus::Invalid;
            }
            alpha_new.opacity = static_cast<float>(op_v->as_number());  // E1-03 建立時 clamp 至 [0,1]
        }
    }
    if (const ds::format::Value* v = definition.find(kKeyPosition)) {
        if (!v->is_map()) {
            return PortraitStatus::Invalid;
        }
        if (const ds::format::Value* a = v->find(kKeyPosAnchor)) {
            ds::kernel::Anchor anchor;
            if (!a->is_string() || !ds::kernel::anchor_from_name(a->as_string(), anchor)) {
                return PortraitStatus::Invalid;  // 透傳 E1-08 anchor_from_name（九宮具名錨點）
            }
            position_new.anchor = anchor;
        }
        if (const ds::format::Value* dx_v = v->find(kKeyPosDx)) {
            if (!dx_v->is_number() || !std::isfinite(dx_v->as_number())) {
                return PortraitStatus::Invalid;
            }
            position_new.offset.dx = static_cast<float>(dx_v->as_number());
        }
        if (const ds::format::Value* dy_v = v->find(kKeyPosDy)) {
            if (!dy_v->is_number() || !std::isfinite(dy_v->as_number())) {
                return PortraitStatus::Invalid;
            }
            position_new.offset.dy = static_cast<float>(dy_v->as_number());
        }
    }

    struct PendingExpr {
        std::string name;
        ds::elements::ImageElement image;
    };
    std::vector<PendingExpr> pending;
    if (const ds::format::Value* v = definition.find(kKeyExpressions)) {
        if (!v->is_list()) {
            return PortraitStatus::Invalid;
        }
        for (const auto& item : v->as_list()) {
            if (!item.is_map()) {
                return PortraitStatus::Invalid;
            }
            const ds::format::Value* name_v = item.find(kKeyExprName);
            const ds::format::Value* src_v = item.find(kKeyExprSource);
            const ds::format::Value* w_v = item.find(kKeyExprWidth);
            const ds::format::Value* h_v = item.find(kKeyExprHeight);
            if (name_v == nullptr || !name_v->is_string() || name_v->as_string().empty()) {
                return PortraitStatus::Invalid;
            }
            if (src_v == nullptr || !src_v->is_string() || src_v->as_string().empty()) {
                return PortraitStatus::Invalid;
            }
            if (w_v == nullptr || !w_v->is_number() || h_v == nullptr || !h_v->is_number()) {
                return PortraitStatus::Invalid;
            }
            const std::int64_t w = w_v->as_int();
            const std::int64_t h = h_v->as_int();
            if (w <= 0 || h <= 0) {
                return PortraitStatus::Invalid;
            }
            ds::elements::ScaleMode scale = ds::elements::ScaleMode::Fit;
            if (const ds::format::Value* sc = item.find(kKeyExprScale)) {
                if (!sc->is_string() || !parse_scale_mode(sc->as_string(), scale)) {
                    return PortraitStatus::Invalid;
                }
            }
            for (const auto& p : pending) {
                if (p.name == name_v->as_string()) {
                    return PortraitStatus::Invalid;  // 重複具名表情 id
                }
            }
            ds::elements::MemoryImageSource src(
                src_v->as_string(),
                ds::elements::ImageDimensions{static_cast<int>(w), static_cast<int>(h)});
            ds::elements::ImageElement image;
            if (image.set_source(src) != ds::elements::ImageStatus::Ok) {
                return PortraitStatus::Invalid;  // 防禦性：src 已由上方驗證非空且尺寸 > 0
            }
            image.set_scale_mode(scale);
            pending.push_back(PendingExpr{name_v->as_string(), std::move(image)});
        }
    }

    std::string initial_expr;
    if (const ds::format::Value* v = definition.find(kKeyInitialExpression)) {
        if (!v->is_string() || v->as_string().empty()) {
            return PortraitStatus::Invalid;
        }
        initial_expr = v->as_string();
        bool found = false;
        for (const auto& p : pending) {
            if (p.name == initial_expr) {
                found = true;
                break;
            }
        }
        if (!found) {
            return PortraitStatus::Invalid;  // initial_expression 須匹配 expressions 內某一 name
        }
    } else if (!pending.empty()) {
        initial_expr = pending.front().name;  // 未給且 expressions 非空 → 預設第一個
    }

    // --- 能力閘控（E1-03，NFR-03）：per-pixel alpha 不可用即走降級路徑，不建立任何 surface ---
    if (!alpha_svc_.supported()) {
        return PortraitStatus::Unsupported;
    }

    // --- 跨擴充點實體化（全有或全無：任一步失敗即回滾，不留殘留後端狀態）---
    // 1) E1-03：建立支援 per-pixel alpha 的具名 surface。
    ds::kernel::SurfaceProfile sp;
    sp.layer = layer_new;
    sp.lifecycle = ds::kernel::SurfaceLifecycle::Persistent;  // 立繪常駐桌面

    const ds::kernel::AlphaStatus as = alpha_svc_.create_alpha_surface(id_, sp, alpha_new);
    if (as == ds::kernel::AlphaStatus::Unsupported) {
        return PortraitStatus::Unsupported;  // 能力於建立當下不可用（降級路徑；未建立任何 surface）
    }
    if (as != ds::kernel::AlphaStatus::Ok) {
        return PortraitStatus::Invalid;  // 空 / 重複 id、非有限 opacity 等（不改狀態，無殘留）
    }

    // 2) E1-01：指派到具名圖層（改動堆疊狀態 → 先經 has(kernel.surface) 閘控，NFR-03）。
    const ds::kernel::LayerAssign la = layers_.assign(id_, layer_new);
    if (la == ds::kernel::LayerAssign::RejectedNoCapability) {
        teardown_backend_state();
        return PortraitStatus::Unsupported;  // 圖層能力不可用（NFR-03）→ 回滾
    }
    if (la != ds::kernel::LayerAssign::Ok && la != ds::kernel::LayerAssign::Moved) {
        teardown_backend_state();
        return PortraitStatus::Invalid;  // RejectedEmptyId 等（id 已驗非空，防禦性）
    }

    // 3) E1-08：登錄初始記憶位置（承 E1-07 AnchorSpec；後端已有此 surface）。
    if (drag_.set_position(id_, position_new) != ds::kernel::DragStatus::Ok) {
        teardown_backend_state();
        return PortraitStatus::Invalid;  // 位置已於解讀階段驗證，防禦性回滾
    }

    // 4) E4-06 + E4-02：登記具名表情（純記憶體，皆已於上方驗證，理論不達失敗）。
    for (auto& p : pending) {
        if (!add_expression_impl(p.name, std::move(p.image))) {
            teardown_backend_state();
            return PortraitStatus::Invalid;
        }
    }
    if (!initial_expr.empty() && !switch_expression_impl(initial_expr)) {
        teardown_backend_state();
        return PortraitStatus::Invalid;
    }

    // --- 全數成功：提交期望狀態至成員，轉為 Loaded，並整理 E5-14 命中 surface ---
    layer_ = layer_new;
    alpha_ = alpha_new;
    position_ = position_new;
    state_ = PortraitState::Loaded;
    refresh_hit_surface();
    return PortraitStatus::Ok;
}

bool PortraitProfile::unload() {
    if (state_ != PortraitState::Loaded) {
        return false;  // 未載入，no-op，不靜默。
    }
    teardown_backend_state();
    state_ = PortraitState::Unloaded;
    return true;
}

bool PortraitProfile::add_expression_impl(const std::string& name,
                                          ds::elements::ImageElement image) {
    if (name.empty() || !image.has_source()) {
        return false;
    }
    if (switcher_.has(name)) {
        return false;  // 重複具名表情
    }
    if (switcher_.register_surface(name) != ds::render::SwitchStatus::Ok) {
        return false;
    }
    image.set_target(id_);  // 單一顯示目標：一律指向本 profile 的主要 surface（NFR-02）
    expressions_.push_back({name, std::move(image)});
    return true;
}

bool PortraitProfile::switch_expression_impl(const std::string& name) {
    return switcher_.switch_to(name) == ds::render::SwitchStatus::Ok;
}

bool PortraitProfile::add_expression(const std::string& name, ds::elements::ImageElement image) {
    if (state_ != PortraitState::Loaded) {
        return false;  // 未載入：無主要 surface 可作顯示目標
    }
    if (!add_expression_impl(name, std::move(image))) {
        return false;
    }
    refresh_hit_surface();
    return true;
}

bool PortraitProfile::remove_expression(const std::string& name) {
    if (state_ != PortraitState::Loaded) {
        return false;
    }
    if (switcher_.unregister_surface(name) != ds::render::SwitchStatus::Ok) {
        return false;  // 未知名：不崩潰
    }
    for (auto it = expressions_.begin(); it != expressions_.end(); ++it) {
        if (it->first == name) {
            expressions_.erase(it);
            break;
        }
    }
    refresh_hit_surface();
    return true;
}

bool PortraitProfile::switch_expression(const std::string& name) {
    if (state_ != PortraitState::Loaded) {
        return false;
    }
    if (!switch_expression_impl(name)) {
        return false;
    }
    refresh_hit_surface();
    return true;
}

std::string PortraitProfile::current_expression() const { return switcher_.current(); }

std::vector<std::string> PortraitProfile::expressions() const { return switcher_.list(); }

bool PortraitProfile::has_expression(const std::string& name) const { return switcher_.has(name); }

const ds::elements::ImageElement* PortraitProfile::current_image() const {
    if (!switcher_.has_current()) {
        return nullptr;
    }
    return find_expression(switcher_.current());
}

bool PortraitProfile::set_regions(ds::kernel::NamedRegionMap regions) {
    if (state_ != PortraitState::Loaded) {
        return false;  // 未載入：無主要 surface 可登記子區域
    }
    regions_.set_regions(id_, std::move(regions));
    return true;
}

bool PortraitProfile::has_regions() const { return regions_.has_regions(id_); }

ds::events::SubscriptionId PortraitProfile::on_region_click(ds::events::RegionEventListener listener) {
    return regions_.subscribe(id_, std::move(listener));
}

bool PortraitProfile::unsubscribe_region_click(ds::events::SubscriptionId id) {
    return regions_.unsubscribe(id);
}

ds::events::RouteStatus PortraitProfile::inject_click(const ds::kernel::LocalPoint& point) {
    return regions_.inject_click(ds::events::MouseButton::Left, point);
}

ds::kernel::DragStatus PortraitProfile::place(const ds::kernel::AnchorSpec& spec) {
    if (state_ != PortraitState::Loaded) {
        return ds::kernel::DragStatus::Invalid;  // 未載入：無 surface 可定位
    }
    return drag_.set_position(id_, spec);
}

ds::kernel::AnchorStatus PortraitProfile::resolve_live(const ds::kernel::Size& container,
                                                       const ds::kernel::Size& element,
                                                       ds::kernel::ResolvedPlacement& out) const {
    return drag_.resolve_live(id_, container, element, out);
}

ds::kernel::DragStatus PortraitProfile::begin_drag() {
    if (state_ != PortraitState::Loaded) {
        return ds::kernel::DragStatus::Invalid;  // 未載入：無位置可拖
    }
    return drag_.begin_drag(id_);
}

ds::kernel::DragStatus PortraitProfile::drag_to(const ds::kernel::AnchorSpec& spec) {
    if (state_ != PortraitState::Loaded) {
        return ds::kernel::DragStatus::NotDragging;  // 未載入必然未在拖曳
    }
    return drag_.drag_to(id_, spec);
}

ds::kernel::DragStatus PortraitProfile::end_drag() {
    if (state_ != PortraitState::Loaded) {
        return ds::kernel::DragStatus::NotDragging;
    }
    return drag_.end_drag(id_);
}

ds::kernel::DragStatus PortraitProfile::cancel_drag() {
    if (state_ != PortraitState::Loaded) {
        return ds::kernel::DragStatus::NotDragging;
    }
    return drag_.cancel_drag(id_);
}

bool PortraitProfile::is_dragging() const { return drag_.is_dragging(id_); }

std::string PortraitProfile::save_position() const { return drag_.serialize_positions(); }

ds::kernel::DragStatus PortraitProfile::load_position(const std::string& text) {
    return drag_.load_positions(text);
}

}  // namespace ds::profiles
