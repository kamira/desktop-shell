// content/profiles/c1_01/skin_profile.cpp — C1-01 Skin profile 實作（組裝型 artifact 單元）
//
// 相位 1：純資料 / 邏輯組裝，無真實 GUI、無平台分支（無 #ifdef / win32 / cocoa）、無絕對座標 /
// 數字 z-order（NFR-02）。宣告式定義（E7-01）解讀後跨 E1-03 / E1-01 / E1-02 / E1-08 實體化；
// per-pixel alpha 能力經 has() 閘控（NFR-03）；無效輸入結構化回報、中途失敗回滾，不靜默。
#include "skin_profile.hpp"

#include <cmath>    // std::isfinite
#include <utility>  // std::move

namespace ds::profiles {

namespace {

// 宣告式 skin 定義的欄位名（NFR-02：位置以正規化偏移承載，非像素座標）。
constexpr const char* kKeyLayer = "layer";
constexpr const char* kKeyInput = "input";
constexpr const char* kKeyAlpha = "alpha";
constexpr const char* kKeyPosition = "position";
constexpr const char* kKeyAlphaMode = "mode";
constexpr const char* kKeyAlphaOpacity = "opacity";
constexpr const char* kKeyPosAnchor = "anchor";
constexpr const char* kKeyPosDx = "dx";
constexpr const char* kKeyPosDy = "dy";

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

// 具名輸入策略字串 → E1-02 InputStrategy。未知回 false（不靜默）。
bool parse_strategy(const std::string& name, ds::kernel::InputStrategy& out) {
    if (name == "interactive") {
        out = ds::kernel::InputStrategy::Interactive;
    } else if (name == "capture") {
        out = ds::kernel::InputStrategy::Capture;
    } else if (name == "click-through") {
        out = ds::kernel::InputStrategy::ClickThrough;
    } else if (name == "inert") {
        out = ds::kernel::InputStrategy::Inert;
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

}  // namespace

const char* to_string(SkinState s) noexcept {
    switch (s) {
        case SkinState::Unloaded:
            return "Unloaded";
        case SkinState::Loaded:
            return "Loaded";
    }
    return "unknown";
}

const char* to_string(SkinStatus s) noexcept {
    switch (s) {
        case SkinStatus::Ok:
            return "Ok";
        case SkinStatus::Unsupported:
            return "Unsupported";
        case SkinStatus::Invalid:
            return "Invalid";
        case SkinStatus::AlreadyLoaded:
            return "AlreadyLoaded";
        case SkinStatus::NotLoaded:
            return "NotLoaded";
    }
    return "unknown";
}

SkinProfile::SkinProfile(std::string id, ds::kernel::KernelBackend& backend,
                         ds::kernel::LayerStack& layers)
    : id_(std::move(id)),
      backend_(backend),
      layers_(layers),
      alpha_svc_(backend_),
      input_ctl_(backend_),
      drag_(backend_) {}

SkinProfile::~SkinProfile() {
    if (state_ == SkinState::Loaded) {
        unload();
    }
}

void SkinProfile::teardown_backend_state() {
    // 反向拆除（各步對未知 id 皆安全 no-op）：位置登錄 → 輸入策略 → 圖層指派 → alpha surface。
    drag_.forget(id_);
    input_ctl_.forget(id_);
    layers_.remove(id_);
    alpha_svc_.destroy_alpha_surface(id_);
}

SkinStatus SkinProfile::load_skin(const ds::format::Value& definition) {
    if (state_ == SkinState::Loaded) {
        return SkinStatus::AlreadyLoaded;  // 不靜默重載；呼叫端須先 unload()。
    }
    if (id_.empty()) {
        return SkinStatus::Invalid;
    }
    if (!definition.is_map()) {
        return SkinStatus::Invalid;  // 宣告式定義須為 Map（通常為 Document::root）。
    }

    // --- 解讀宣告式定義為期望狀態（暫存；全數驗證通過且實體化成功才提交至成員）---
    // 以目前預設起始，逐一以宣告式欄位覆寫；任一已知欄位型別 / 具名值不合法即 Invalid（不改狀態）。
    ds::kernel::SurfaceLayer layer_new = layer_;
    ds::kernel::InputStrategy strategy_new = strategy_;
    ds::kernel::AlphaProfile alpha_new = alpha_;
    ds::kernel::AnchorSpec position_new = position_;

    if (const ds::format::Value* v = definition.find(kKeyLayer)) {
        if (!v->is_string() || !parse_layer(v->as_string(), layer_new)) {
            return SkinStatus::Invalid;
        }
    }
    if (const ds::format::Value* v = definition.find(kKeyInput)) {
        if (!v->is_string() || !parse_strategy(v->as_string(), strategy_new)) {
            return SkinStatus::Invalid;
        }
    }
    if (const ds::format::Value* v = definition.find(kKeyAlpha)) {
        if (!v->is_map()) {
            return SkinStatus::Invalid;
        }
        if (const ds::format::Value* mode_v = v->find(kKeyAlphaMode)) {
            if (!mode_v->is_string() || !parse_alpha_mode(mode_v->as_string(), alpha_new.mode)) {
                return SkinStatus::Invalid;
            }
        }
        if (const ds::format::Value* op_v = v->find(kKeyAlphaOpacity)) {
            if (!op_v->is_number() || !std::isfinite(op_v->as_number())) {
                return SkinStatus::Invalid;
            }
            alpha_new.opacity = static_cast<float>(op_v->as_number());  // E1-03 建立時 clamp 至 [0,1]
        }
    }
    if (const ds::format::Value* v = definition.find(kKeyPosition)) {
        if (!v->is_map()) {
            return SkinStatus::Invalid;
        }
        const ds::format::Value* anchor_v = v->find(kKeyPosAnchor);
        if (anchor_v != nullptr) {
            ds::kernel::Anchor anchor;
            if (!anchor_v->is_string() ||
                !ds::kernel::anchor_from_name(anchor_v->as_string(), anchor)) {
                return SkinStatus::Invalid;  // 透傳 E1-08 anchor_from_name（九宮具名錨點）
            }
            position_new.anchor = anchor;
        }
        if (const ds::format::Value* dx_v = v->find(kKeyPosDx)) {
            if (!dx_v->is_number() || !std::isfinite(dx_v->as_number())) {
                return SkinStatus::Invalid;
            }
            position_new.offset.dx = static_cast<float>(dx_v->as_number());
        }
        if (const ds::format::Value* dy_v = v->find(kKeyPosDy)) {
            if (!dy_v->is_number() || !std::isfinite(dy_v->as_number())) {
                return SkinStatus::Invalid;
            }
            position_new.offset.dy = static_cast<float>(dy_v->as_number());
        }
    }

    // --- 能力閘控（E1-03，NFR-03）：per-pixel alpha 不可用即走降級路徑，不建立任何 surface ---
    if (!alpha_svc_.supported()) {
        return SkinStatus::Unsupported;
    }

    // --- 跨擴充點實體化（全有或全無：任一步失敗即回滾，不留殘留後端狀態）---
    // 1) E1-03：建立支援 per-pixel alpha 的具名 surface（四參數 profile 亦帶入 E1-02 對映策略）。
    ds::kernel::SurfaceProfile sp;
    sp.layer = layer_new;
    sp.input = ds::kernel::to_backend_policy(strategy_new);
    sp.hit = (ds::kernel::hit_result(strategy_new) == ds::kernel::HitResult::Solid)
                 ? ds::kernel::HitPolicy::Solid
                 : ds::kernel::HitPolicy::Transparent;
    sp.lifecycle = ds::kernel::SurfaceLifecycle::Persistent;  // 桌面角色常駐

    const ds::kernel::AlphaStatus as = alpha_svc_.create_alpha_surface(id_, sp, alpha_new);
    if (as == ds::kernel::AlphaStatus::Unsupported) {
        return SkinStatus::Unsupported;  // 能力於建立當下不可用（降級路徑；未建立任何 surface）
    }
    if (as != ds::kernel::AlphaStatus::Ok) {
        return SkinStatus::Invalid;  // 空 / 重複 id、非有限 opacity 等（不改狀態，無殘留）
    }

    // 2) E1-01：指派到具名圖層（改動堆疊狀態 → 先經 has(kernel.surface) 閘控，NFR-03）。
    const ds::kernel::LayerAssign la = layers_.assign(id_, layer_new);
    if (la == ds::kernel::LayerAssign::RejectedNoCapability) {
        teardown_backend_state();
        return SkinStatus::Unsupported;  // 圖層能力不可用（NFR-03）→ 回滾
    }
    if (la != ds::kernel::LayerAssign::Ok && la != ds::kernel::LayerAssign::Moved) {
        teardown_backend_state();
        return SkinStatus::Invalid;  // RejectedEmptyId 等（id 已驗非空，防禦性）
    }

    // 3) E1-02：設定輸入策略（surface 已存在於後端；Capture 需 capture 能力，NFR-03）。
    if (!input_ctl_.set_strategy(id_, strategy_new)) {
        teardown_backend_state();
        return SkinStatus::Unsupported;  // 典型為 Capture 於此後端無 capture 能力 → 回滾
    }

    // 4) E1-08：登錄初始記憶位置（承 E1-07 AnchorSpec；後端已有此 surface）。
    if (drag_.set_position(id_, position_new) != ds::kernel::DragStatus::Ok) {
        teardown_backend_state();
        return SkinStatus::Invalid;  // 位置已於解讀階段驗證，防禦性回滾
    }

    // --- 全數成功：提交期望狀態至成員，轉為 Loaded ---
    layer_ = layer_new;
    strategy_ = strategy_new;
    alpha_ = alpha_new;
    position_ = position_new;
    state_ = SkinState::Loaded;
    return SkinStatus::Ok;
}

bool SkinProfile::unload() {
    if (state_ != SkinState::Loaded) {
        return false;  // 未載入，no-op，不靜默。
    }
    teardown_backend_state();
    state_ = SkinState::Unloaded;
    return true;
}

ds::kernel::DragStatus SkinProfile::place(const ds::kernel::AnchorSpec& spec) {
    if (state_ != SkinState::Loaded) {
        return ds::kernel::DragStatus::Invalid;  // 未載入：無 surface 可定位
    }
    return drag_.set_position(id_, spec);
}

ds::kernel::AnchorStatus SkinProfile::resolve_live(const ds::kernel::Size& container,
                                                   const ds::kernel::Size& element,
                                                   ds::kernel::ResolvedPlacement& out) const {
    return drag_.resolve_live(id_, container, element, out);
}

ds::kernel::DragStatus SkinProfile::begin_drag() {
    if (state_ != SkinState::Loaded) {
        return ds::kernel::DragStatus::Invalid;  // 未載入：無位置可拖
    }
    return drag_.begin_drag(id_);
}

ds::kernel::DragStatus SkinProfile::drag_to(const ds::kernel::AnchorSpec& spec) {
    if (state_ != SkinState::Loaded) {
        return ds::kernel::DragStatus::NotDragging;  // 未載入必然未在拖曳
    }
    return drag_.drag_to(id_, spec);
}

ds::kernel::DragStatus SkinProfile::end_drag() {
    if (state_ != SkinState::Loaded) {
        return ds::kernel::DragStatus::NotDragging;
    }
    return drag_.end_drag(id_);
}

ds::kernel::DragStatus SkinProfile::cancel_drag() {
    if (state_ != SkinState::Loaded) {
        return ds::kernel::DragStatus::NotDragging;
    }
    return drag_.cancel_drag(id_);
}

bool SkinProfile::is_dragging() const { return drag_.is_dragging(id_); }

std::string SkinProfile::save_position() const { return drag_.serialize_positions(); }

ds::kernel::DragStatus SkinProfile::load_position(const std::string& text) {
    return drag_.load_positions(text);
}

}  // namespace ds::profiles
