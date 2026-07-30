// E1-03 逐像素 alpha surface — 實作
//
// 能力閘控 + null 記憶體模擬；不含任何平台分支、真實 OS / 繪圖 API。
#include "alpha_surface.hpp"

#include <algorithm>  // std::clamp
#include <cmath>      // std::isfinite
#include <utility>

namespace ds::kernel {

// --- 內部尋找（具名鍵線性掃描；alpha surface 數量小）---
AlphaSurfaceService::Record* AlphaSurfaceService::find(const SurfaceId& id) {
    for (auto& r : records_) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

const AlphaSurfaceService::Record* AlphaSurfaceService::find(
    const SurfaceId& id) const {
    for (const auto& r : records_) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

namespace {

// 正規化不透明度：非有限值視為無效（回 false）；有效則 clamp 至 [0,1]。
bool normalize_opacity(float in, float& out) {
    if (!std::isfinite(in)) {
        return false;
    }
    out = std::clamp(in, 0.0f, 1.0f);
    return true;
}

}  // namespace

// --- 建立 / 銷毀 ---
AlphaStatus AlphaSurfaceService::create_alpha_surface(
    const SurfaceId& id, const SurfaceProfile& profile,
    const AlphaProfile& alpha) {
    // NFR-03 能力閘控：能力不可用時結構化拒絕，不建立任何 surface（降級路徑）。
    if (!supported()) {
        return AlphaStatus::Unsupported;
    }
    if (id.empty() || find(id) != nullptr) {
        return AlphaStatus::Invalid;  // 空 id 或已是 alpha surface
    }
    float opacity = 1.0f;
    if (!normalize_opacity(alpha.opacity, opacity)) {
        return AlphaStatus::Invalid;  // opacity 非有限值
    }
    // 委由後端 K1 原語建立實體 surface；後端拒絕（含 id 已存在於後端）→ Invalid，不留半份狀態。
    if (!backend_.create_surface(id, profile)) {
        return AlphaStatus::Invalid;
    }
    Record rec;
    rec.id = id;
    rec.alpha.mode = alpha.mode;
    rec.alpha.opacity = opacity;
    records_.push_back(std::move(rec));
    return AlphaStatus::Ok;
}

AlphaStatus AlphaSurfaceService::destroy_alpha_surface(const SurfaceId& id) {
    for (auto it = records_.begin(); it != records_.end(); ++it) {
        if (it->id == id) {
            backend_.destroy_surface(id);  // 同步銷毀後端實體 surface（未知亦安全）
            records_.erase(it);
            return AlphaStatus::Ok;
        }
    }
    return AlphaStatus::Invalid;  // 未知 id：不崩潰
}

// --- alpha 設定 / 查詢 ---
AlphaStatus AlphaSurfaceService::set_alpha(const SurfaceId& id,
                                           const AlphaProfile& alpha) {
    if (!supported()) {
        return AlphaStatus::Unsupported;  // 能力閘控（NFR-03）
    }
    Record* rec = find(id);
    if (rec == nullptr) {
        return AlphaStatus::Invalid;
    }
    float opacity = 1.0f;
    if (!normalize_opacity(alpha.opacity, opacity)) {
        return AlphaStatus::Invalid;
    }
    rec->alpha.mode = alpha.mode;
    rec->alpha.opacity = opacity;
    return AlphaStatus::Ok;
}

AlphaStatus AlphaSurfaceService::set_mode(const SurfaceId& id, AlphaMode mode) {
    if (!supported()) {
        return AlphaStatus::Unsupported;
    }
    Record* rec = find(id);
    if (rec == nullptr) {
        return AlphaStatus::Invalid;
    }
    rec->alpha.mode = mode;
    return AlphaStatus::Ok;
}

AlphaStatus AlphaSurfaceService::set_opacity(const SurfaceId& id, float opacity) {
    if (!supported()) {
        return AlphaStatus::Unsupported;
    }
    Record* rec = find(id);
    if (rec == nullptr) {
        return AlphaStatus::Invalid;
    }
    float normalized = 1.0f;
    if (!normalize_opacity(opacity, normalized)) {
        return AlphaStatus::Invalid;
    }
    rec->alpha.opacity = normalized;
    return AlphaStatus::Ok;
}

const AlphaProfile* AlphaSurfaceService::alpha_profile(const SurfaceId& id) const {
    const Record* rec = find(id);
    return rec != nullptr ? &rec->alpha : nullptr;
}

bool AlphaSurfaceService::is_per_pixel(const SurfaceId& id) const {
    const Record* rec = find(id);
    return rec != nullptr && rec->alpha.mode == AlphaMode::PerPixel;
}

// --- 能力矩陣輔助 ---
CapabilityMatrix alpha_capable_matrix() {
    // 以上游保守 defaults() 為基礎，追加宣告 per-pixel alpha 為「可用的可選能力」。
    // CapabilityMatrix 對重複 id「後定義者為準」，此處為新鍵，直接生效。
    std::vector<CapabilityDecl> decls = CapabilityMatrix::defaults().all();
    decls.push_back(CapabilityDecl{
        kPerPixelAlphaCapability,
        "逐像素 alpha 透明 surface（部分桌面環境 / 合成器不支援）",
        /*optional=*/true,
        /*default_available=*/true,
    });
    return CapabilityMatrix(std::move(decls));
}

CapabilityMatrix alpha_incapable_matrix() {
    // 上游保守預設：未宣告 per-pixel alpha 鍵，has() 因此回 false（能力不可用）。
    return CapabilityMatrix::defaults();
}

}  // namespace ds::kernel
