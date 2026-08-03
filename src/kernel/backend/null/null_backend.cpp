// E1-24 null 後端參考實作 — NullKernelBackend 實作
//
// 全部平台操作為 no-op 或以記憶體狀態承接；此檔不含任何平台分支或真實 OS API，
// 任何平台皆可編譯執行。
#include "null_backend.hpp"

#include <utility>

namespace ds::kernel {

NullKernelBackend::NullKernelBackend(CapabilityMatrix caps)
    : caps_(std::move(caps)) {}

// --- 內部尋找（具名鍵線性掃描）---
NullKernelBackend::SurfaceRecord* NullKernelBackend::find(const SurfaceId& id) {
    for (auto& e : surfaces_) {
        if (e.first == id) {
            return &e.second;
        }
    }
    return nullptr;
}

const NullKernelBackend::SurfaceRecord* NullKernelBackend::find(
    const SurfaceId& id) const {
    for (const auto& e : surfaces_) {
        if (e.first == id) {
            return &e.second;
        }
    }
    return nullptr;
}

// --- 生命週期 ---
bool NullKernelBackend::init() {
    ++init_calls_;
    initialized_ = true;  // 冪等：重複 init 仍為已初始化
    return true;
}

void NullKernelBackend::shutdown() {
    ++shutdown_calls_;
    // 釋放所有記憶體 surface；冪等：未初始化再呼叫亦安全。
    surfaces_.clear();
    initialized_ = false;
}

// --- K1 surface kernel ---
bool NullKernelBackend::create_surface(const SurfaceId& id,
                                       const SurfaceProfile& profile) {
    // 前置條件：後端須已 init()。
    //
    // CHG-20260803-11（對齊 K-007）：此檢查原本不存在——null 後端允許未初始化就建立 surface，
    // 而 win32 後端不允許（真實後端必須先註冊視窗類別，是物理限制）。分歧自相位 1 存在，
    // 因為舊契約測的是它自己的 stub，從未跑過真實後端（K-003）。
    // 對齊方向取 **win32 的嚴格版**：未初始化的後端不該能開出視窗。
    if (!initialized_ || id.empty() || find(id) != nullptr) {
        return false;  // 保守：未初始化、空 id、重複 id 一律拒絕
    }
    SurfaceRecord rec;
    rec.profile = profile;
    surfaces_.emplace_back(id, std::move(rec));
    return true;
}

bool NullKernelBackend::destroy_surface(const SurfaceId& id) {
    for (auto it = surfaces_.begin(); it != surfaces_.end(); ++it) {
        if (it->first == id) {
            surfaces_.erase(it);
            return true;
        }
    }
    return false;  // 未知 id：不崩潰
}

bool NullKernelBackend::has_surface(const SurfaceId& id) const {
    return find(id) != nullptr;
}

bool NullKernelBackend::show_surface(const SurfaceId& id) {
    SurfaceRecord* rec = find(id);
    if (rec == nullptr) {
        return false;
    }
    rec->visible = true;
    return true;
}

bool NullKernelBackend::hide_surface(const SurfaceId& id) {
    SurfaceRecord* rec = find(id);
    if (rec == nullptr) {
        return false;
    }
    rec->visible = false;
    return true;
}

bool NullKernelBackend::is_visible(const SurfaceId& id) const {
    const SurfaceRecord* rec = find(id);
    return rec != nullptr && rec->visible;  // 未知 id 保守回 false
}

const SurfaceProfile* NullKernelBackend::surface_profile(
    const SurfaceId& id) const {
    const SurfaceRecord* rec = find(id);
    return rec != nullptr ? &rec->profile : nullptr;
}

// --- K2 繪製 ---
bool NullKernelBackend::begin_frame(const SurfaceId& id) {
    SurfaceRecord* rec = find(id);
    if (rec == nullptr || rec->in_frame) {
        return false;  // 未知 id 或已在 frame 中：拒絕（不可重入 begin）
    }
    rec->in_frame = true;  // no-op 繪製：僅記錄狀態
    return true;
}

bool NullKernelBackend::end_frame(const SurfaceId& id) {
    SurfaceRecord* rec = find(id);
    if (rec == nullptr || !rec->in_frame) {
        return false;  // 未曾 begin：拒絕
    }
    rec->in_frame = false;
    ++rec->completed_frames;
    return true;
}

// --- K3 輸入 ---
bool NullKernelBackend::set_input_policy(const SurfaceId& id, InputPolicy policy) {
    SurfaceRecord* rec = find(id);
    if (rec == nullptr) {
        return false;
    }
    rec->profile.input = policy;
    return true;
}

std::vector<InputEvent> NullKernelBackend::poll_input() {
    ++poll_input_calls_;
    return {};  // null 後端無真實輸入來源，永遠回空
}

// --- 記錄狀態查詢 ---
std::size_t NullKernelBackend::completed_frames(const SurfaceId& id) const {
    const SurfaceRecord* rec = find(id);
    return rec != nullptr ? rec->completed_frames : 0;
}

bool NullKernelBackend::in_frame(const SurfaceId& id) const {
    const SurfaceRecord* rec = find(id);
    return rec != nullptr && rec->in_frame;
}

}  // namespace ds::kernel
