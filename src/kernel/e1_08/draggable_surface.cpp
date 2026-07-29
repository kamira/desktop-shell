// E1-08 自由拖曳與位置記憶 — 實作（拖曳狀態機 + 位置記憶持久化）
//
// 相位 1：無真實視窗 / 繪圖 API、無平台分支（無 #ifdef / win32 / cocoa）。拖曳為注入式狀態機，
// 位置以宣告式 AnchorSpec（E1-07）承載，持久化經 E7-12 設定值寫回落回 E7-01 文字格式。
// 無效輸入結構化回報，不靜默、不崩潰。
#include "draggable_surface.hpp"

#include <cmath>  // std::isfinite

#include "document.hpp"   // E7-01（經 E7-12 傳遞，可讀不可改）：Value / Document / parse
#include "writeback.hpp"  // E7-12（可讀不可改）：set_value / serialize（其上為 E7-01 格式核心）

namespace ds::kernel {

namespace {

// AnchorSpec 是否合法：anchor 為合法九宮列舉且 offset 有限（與 E1-07 內部判準一致）。
bool is_valid_spec(const AnchorSpec& spec) {
    return is_valid_anchor(spec.anchor) && std::isfinite(spec.offset.dx) &&
           std::isfinite(spec.offset.dy);
}

// 位置記憶的持久化欄位名（NFR-02：offset 為容器尺寸正規化分數，非像素座標）。
constexpr const char* kFieldAnchor = "anchor";
constexpr const char* kFieldDx = "dx";
constexpr const char* kFieldDy = "dy";

}  // namespace

// ---------------------------------------------------------------------------
// 九宮具名錨點 ↔ 穩定字串名稱
// ---------------------------------------------------------------------------

const char* anchor_to_name(Anchor a) {
    switch (a) {
        case Anchor::TopLeft:      return "top-left";
        case Anchor::TopCenter:    return "top-center";
        case Anchor::TopRight:     return "top-right";
        case Anchor::CenterLeft:   return "center-left";
        case Anchor::Center:       return "center";
        case Anchor::CenterRight:  return "center-right";
        case Anchor::BottomLeft:   return "bottom-left";
        case Anchor::BottomCenter: return "bottom-center";
        case Anchor::BottomRight:  return "bottom-right";
    }
    return "";  // 越界 static_cast → 空名（呼叫端可據以判無效，不靜默填預設）
}

bool anchor_from_name(const std::string& name, Anchor& out) {
    struct Entry {
        const char* name;
        Anchor anchor;
    };
    static const Entry kTable[] = {
        {"top-left", Anchor::TopLeft},         {"top-center", Anchor::TopCenter},
        {"top-right", Anchor::TopRight},       {"center-left", Anchor::CenterLeft},
        {"center", Anchor::Center},            {"center-right", Anchor::CenterRight},
        {"bottom-left", Anchor::BottomLeft},   {"bottom-center", Anchor::BottomCenter},
        {"bottom-right", Anchor::BottomRight},
    };
    for (const auto& e : kTable) {
        if (name == e.name) {
            out = e.anchor;
            return true;
        }
    }
    return false;  // 未知名 → 不觸碰 out（不靜默）
}

// ---------------------------------------------------------------------------
// DraggableSurface —— 具名鍵尋找
// ---------------------------------------------------------------------------

DraggableSurface::Record* DraggableSurface::find(const SurfaceId& id) {
    for (auto& r : positions_) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

const DraggableSurface::Record* DraggableSurface::find(const SurfaceId& id) const {
    for (const auto& r : positions_) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

DraggableSurface::DragState* DraggableSurface::find_drag(const SurfaceId& id) {
    for (auto& d : drags_) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

const DraggableSurface::DragState* DraggableSurface::find_drag(const SurfaceId& id) const {
    for (const auto& d : drags_) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 註冊 / 更新位置
// ---------------------------------------------------------------------------

DragStatus DraggableSurface::set_position(const SurfaceId& id, const AnchorSpec& spec) {
    if (id.empty() || !is_valid_spec(spec)) {
        return DragStatus::Invalid;
    }
    if (!backend_.has_surface(id)) {
        return DragStatus::Invalid;  // 只登錄真實存在的 surface（後端閘控）
    }
    if (find_drag(id) != nullptr) {
        return DragStatus::Invalid;  // 拖曳期間不可外部改位置（避免與 pending 競態）
    }
    if (Record* existing = find(id)) {
        existing->spec = spec;  // 就地更新
    } else {
        positions_.push_back(Record{id, spec});
    }
    return DragStatus::Ok;
}

DragStatus DraggableSurface::forget(const SurfaceId& id) {
    Record* rec = find(id);
    if (rec == nullptr) {
        return DragStatus::Invalid;  // 未註冊
    }
    // 同時清掉任何進行中的拖曳狀態。
    for (auto it = drags_.begin(); it != drags_.end(); ++it) {
        if (it->id == id) {
            drags_.erase(it);
            break;
        }
    }
    for (auto it = positions_.begin(); it != positions_.end(); ++it) {
        if (it->id == id) {
            positions_.erase(it);
            break;
        }
    }
    return DragStatus::Ok;
}

// ---------------------------------------------------------------------------
// 拖曳狀態機
// ---------------------------------------------------------------------------

DragStatus DraggableSurface::begin_drag(const SurfaceId& id) {
    if (id.empty()) {
        return DragStatus::Invalid;
    }
    const Record* rec = find(id);
    if (rec == nullptr) {
        return DragStatus::Invalid;  // 未註冊：無位置可拖
    }
    if (!backend_.has_surface(id)) {
        return DragStatus::Invalid;  // 後端已無此 surface（例如已銷毀）
    }
    if (find_drag(id) != nullptr) {
        return DragStatus::AlreadyDragging;
    }
    // 記錄起始位置；pending 初始化為起始位置（尚未移動）。
    drags_.push_back(DragState{id, rec->spec, rec->spec});
    return DragStatus::Ok;
}

DragStatus DraggableSurface::drag_to(const SurfaceId& id, const AnchorSpec& spec) {
    DragState* drag = find_drag(id);
    if (drag == nullptr) {
        return DragStatus::NotDragging;
    }
    if (!is_valid_spec(spec)) {
        return DragStatus::Invalid;  // 無效目標位置：不更新 pending（不靜默）
    }
    drag->pending = spec;
    return DragStatus::Ok;
}

DragStatus DraggableSurface::end_drag(const SurfaceId& id) {
    DragState* drag = find_drag(id);
    if (drag == nullptr) {
        return DragStatus::NotDragging;
    }
    // 提交 pending 為記憶位置（committed）——「放開後記住」。
    const AnchorSpec committed = drag->pending;
    if (Record* rec = find(id)) {
        rec->spec = committed;
    } else {
        positions_.push_back(Record{id, committed});  // 理論不達（拖曳前提為已註冊）
    }
    for (auto it = drags_.begin(); it != drags_.end(); ++it) {
        if (it->id == id) {
            drags_.erase(it);
            break;
        }
    }
    return DragStatus::Ok;
}

DragStatus DraggableSurface::cancel_drag(const SurfaceId& id) {
    for (auto it = drags_.begin(); it != drags_.end(); ++it) {
        if (it->id == id) {
            drags_.erase(it);  // 放棄 pending；committed 不變（等同還原）
            return DragStatus::Ok;
        }
    }
    return DragStatus::NotDragging;
}

// ---------------------------------------------------------------------------
// 位置查詢
// ---------------------------------------------------------------------------

const AnchorSpec* DraggableSurface::live_position(const SurfaceId& id) const {
    if (const DragState* drag = find_drag(id)) {
        return &drag->pending;  // 拖曳中：目前目標位置
    }
    const Record* rec = find(id);
    return rec ? &rec->spec : nullptr;  // 否則：committed 記憶位置
}

AnchorStatus DraggableSurface::resolve_live(const SurfaceId& id, const Size& container,
                                            const Size& element, ResolvedPlacement& out) const {
    const AnchorSpec* spec = live_position(id);
    if (spec == nullptr) {
        return AnchorStatus::Invalid;  // 未註冊且未拖曳
    }
    return resolve(*spec, container, element, out);  // 委由 E1-07 純佈局計算
}

// ---------------------------------------------------------------------------
// 位置記憶持久化（經 E7-12 設定值寫回 → E7-01 文字格式）
// ---------------------------------------------------------------------------

std::string DraggableSurface::serialize_positions() const {
    using ds::format::Path;
    using ds::format::PathSegment;
    using ds::format::set_value;
    using ds::format::Value;

    // 以 E7-12 set_value 逐欄寫入一棵設定樹（純函式，回傳新樹）。
    // 用**顯式 Path 段**（PathSegment::of_key）而非字串路徑，故含 '.' 的具名 SurfaceId
    // （如 "surface.panel"）不會被字串路徑的 '.' 分隔誤拆——id 恆為單一鍵段。
    Value root = Value::map({});
    for (const auto& r : positions_) {
        const Path anchor_path = {PathSegment::of_key(r.id), PathSegment::of_key(kFieldAnchor)};
        const Path dx_path = {PathSegment::of_key(r.id), PathSegment::of_key(kFieldDx)};
        const Path dy_path = {PathSegment::of_key(r.id), PathSegment::of_key(kFieldDy)};
        root = set_value(root, anchor_path, Value::string(anchor_to_name(r.spec.anchor)));
        // dx / dy 以非整數 Number 承載（正規化分數；序列化保留小數點，再解析型別不漂移）。
        root = set_value(root, dx_path, Value::number(r.spec.offset.dx));
        root = set_value(root, dy_path, Value::number(r.spec.offset.dy));
    }
    return ds::format::serialize(root, ds::format::kSupportedFormat);
}

DragStatus DraggableSurface::load_positions(const std::string& text) {
    using ds::format::parse;
    using ds::format::ParseResult;
    using ds::format::Value;

    const ParseResult result = parse(text);
    if (!result) {
        return DragStatus::Invalid;  // 文字無法解析（帶行號錯誤）——不靜默
    }
    const Value& root = result.document().root;  // 恆為 Map（不含 format_version）

    // 全有或全無：先驗證並收集所有條目，任一不合即整批放棄（不留半套狀態）。
    std::vector<Record> loaded;
    for (const auto& member : root.as_map()) {
        const SurfaceId& id = member.first;
        const Value& entry = member.second;
        if (id.empty() || !entry.is_map()) {
            return DragStatus::Invalid;
        }
        const Value* anchor_v = entry.find(kFieldAnchor);
        const Value* dx_v = entry.find(kFieldDx);
        const Value* dy_v = entry.find(kFieldDy);
        if (anchor_v == nullptr || !anchor_v->is_string() || dx_v == nullptr ||
            !dx_v->is_number() || dy_v == nullptr || !dy_v->is_number()) {
            return DragStatus::Invalid;  // 結構 / 型別不符
        }
        Anchor anchor;
        if (!anchor_from_name(anchor_v->as_string(), anchor)) {
            return DragStatus::Invalid;  // anchor 名無效
        }
        AnchorSpec spec;
        spec.anchor = anchor;
        spec.offset.dx = static_cast<float>(dx_v->as_number());
        spec.offset.dy = static_cast<float>(dy_v->as_number());
        if (!is_valid_spec(spec)) {
            return DragStatus::Invalid;  // 非有限 offset 等
        }
        loaded.push_back(Record{id, spec});
    }
    // 全數通過 → 套用（同名就地更新；不經後端閘控，見標頭說明）。
    for (const auto& l : loaded) {
        if (Record* rec = find(l.id)) {
            rec->spec = l.spec;
        } else {
            positions_.push_back(l);
        }
    }
    return DragStatus::Ok;
}

}  // namespace ds::kernel
