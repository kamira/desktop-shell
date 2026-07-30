// E4-27 綁定式合成（著替） — 實作（見 bound_composite.hpp 規格）。
#include "bound_composite.hpp"

#include <utility>  // std::move

namespace ds::elements {

// --- 內部尋找（具名鍵線性掃描；同子系統 / 同建置慣例的槽 / 部件數量小，見 E4-06 / E4-08）---
std::vector<BoundComposite::SlotEntry>::iterator BoundComposite::find_slot(const SlotId& slot) {
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
        if (it->slot_id == slot) {
            return it;
        }
    }
    return slots_.end();
}

std::vector<BoundComposite::SlotEntry>::const_iterator BoundComposite::find_slot(
    const SlotId& slot) const {
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
        if (it->slot_id == slot) {
            return it;
        }
    }
    return slots_.end();
}

const PartOption* BoundComposite::find_part(const SlotEntry& entry, const PartId& part_id) {
    for (const auto& part : entry.parts) {
        if (part.part_id == part_id) {
            return &part;
        }
    }
    return nullptr;
}

// --- 定義槽位 ---
BindStatus BoundComposite::define_slot(const SlotId& slot, const std::vector<PartOption>& parts) {
    if (slot.empty()) {
        return BindStatus::Invalid;  // 空槽名：不定義
    }
    if (parts.empty()) {
        return BindStatus::Invalid;  // 空候選集：不定義
    }
    if (find_slot(slot) != slots_.end()) {
        return BindStatus::Invalid;  // 槽名重複定義：不覆蓋既有定義
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].part_id.empty() || parts[i].surface_id.empty()) {
            return BindStatus::Invalid;  // 候選部件格式不合法：不套用
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (parts[j].part_id == parts[i].part_id) {
                return BindStatus::Invalid;  // 同槽內 part_id 重複：不套用
            }
        }
    }

    SlotEntry entry;
    entry.slot_id = slot;
    entry.parts = parts;
    slots_.push_back(std::move(entry));
    return BindStatus::Ok;
}

// --- 綁定 / 著替 ---
BindStatus BoundComposite::bind(const SlotId& slot, const PartId& part_id) {
    auto it = find_slot(slot);
    if (it == slots_.end()) {
        return BindStatus::NotFound;  // 槽未定義
    }
    if (part_id.empty()) {
        return BindStatus::Invalid;  // 空 part_id：不套用
    }
    if (find_part(*it, part_id) == nullptr) {
        return BindStatus::NotFound;  // 無效 part：不在該槽候選集內
    }
    it->bound_part = part_id;  // 首次綁定與改綁共用同一入口
    return BindStatus::Ok;
}

BindStatus BoundComposite::rebind(const SlotId& slot, const PartId& part_id) {
    auto it = find_slot(slot);
    if (it == slots_.end()) {
        return BindStatus::NotFound;  // 槽未定義
    }
    if (it->bound_part.empty()) {
        return BindStatus::NotFound;  // 尚無既存綁定：沒有舊值可換，請先 bind
    }
    if (part_id.empty()) {
        return BindStatus::Invalid;  // 空 part_id：不套用，維持原綁定
    }
    if (find_part(*it, part_id) == nullptr) {
        return BindStatus::NotFound;  // 無效 part：不套用，維持原綁定
    }
    it->bound_part = part_id;  // 著替成功：下一次 compose() 反映新綁定
    return BindStatus::Ok;
}

// --- 查詢 ---
bool BoundComposite::has_slot(const SlotId& slot) const {
    return find_slot(slot) != slots_.end();
}

bool BoundComposite::is_bound(const SlotId& slot) const {
    auto it = find_slot(slot);
    return it != slots_.end() && !it->bound_part.empty();
}

PartId BoundComposite::current_part(const SlotId& slot) const {
    auto it = find_slot(slot);
    if (it == slots_.end()) {
        return PartId();  // 槽未定義：明確回空，不回傳任意值
    }
    return it->bound_part;  // 未綁定時本身即為空字串
}

std::vector<SlotId> BoundComposite::slot_order() const {
    std::vector<SlotId> order;
    order.reserve(slots_.size());
    for (const auto& entry : slots_) {
        order.push_back(entry.slot_id);
    }
    return order;  // 依定義順序的具名清單，不外露任何內部數字索引語意
}

// --- 合成 ---
CompositeResult BoundComposite::compose() const {
    ds::render::LayerCompositor compositor(switcher_);  // 每次 compose() 全新暫用實例：單次求值

    for (const auto& entry : slots_) {
        if (entry.bound_part.empty()) {
            continue;  // 未綁定槽：明確跳過，不貢獻任何層（文件化行為，非靜默丟資料）
        }
        const PartOption* part = find_part(entry, entry.bound_part);
        // part 恆非 nullptr：bound_part 只可能經 bind/rebind 設定，而兩者都已驗證其屬於
        // entry.parts 才會寫入。
        auto status = compositor.add_layer(part->surface_id, part->blend_mode, part->opacity);
        if (status != ds::render::CompositeStatus::Ok) {
            CompositeResult failed;
            failed.status = status;  // 不回傳部分合成結果：plan 維持預設空值
            return failed;
        }
    }

    CompositeResult result;
    result.status = ds::render::CompositeStatus::Ok;
    result.plan = compositor.compose();
    return result;
}

}  // namespace ds::elements
