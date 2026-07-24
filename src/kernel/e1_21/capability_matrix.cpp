// E1-21 能力矩陣宣告檔 — 實作
//
// 內嵌預設宣告 + 平台中立的查詢邏輯。此檔不含任何平台分支或真實後端。
#include "capability_matrix.hpp"

#include <utility>

namespace ds::kernel {

namespace {

// 內嵌的預設能力矩陣 —— 相位 1 的單一資料來源。
//
// 命名慣例：<子系統>.<能力>。此處只宣告「可能在某些平台不存在」與「保證存在」
// 兩類的代表性能力；真正的可用性在真實後端上線後由後端探測覆寫。
// 純資料、不含平台判斷 —— 換平台時**不動這裡**，只換提供實際探測的後端。
const std::vector<CapabilityDecl>& default_decls() {
    static const std::vector<CapabilityDecl> kDecls = {
        // --- 保證存在的能力（所有目標平台皆有；default_available = true）---
        {"render.paint", "基礎繪製（所有平台皆有）", /*optional=*/false, /*default_available=*/true},
        {"kernel.surface", "surface kernel 視窗表面", /*optional=*/false, /*default_available=*/true},
        {"host.clipboard_read", "讀取剪貼簿", /*optional=*/false, /*default_available=*/true},

        // --- 可選能力（可能在某些平台不存在；呼叫端必須 has() 閘控 + 降級）---
        // 相位 1 尚無真實後端，null 期一律預設不可用（保守），待後端探測後覆寫。
        {"host.tray_icon", "系統匣圖示（某些桌面環境不提供）", /*optional=*/true, /*default_available=*/false},
        {"host.global_hotkey", "全域熱鍵註冊（受平台/權限限制）", /*optional=*/true, /*default_available=*/false},
        {"host.autostart", "開機自啟", /*optional=*/true, /*default_available=*/false},
        {"actuator.brightness", "螢幕亮度控制", /*optional=*/true, /*default_available=*/false},
        {"actuator.wallpaper", "設定桌布", /*optional=*/true, /*default_available=*/false},
        {"actuator.screen_capture", "螢幕擷取（需權限）", /*optional=*/true, /*default_available=*/false},
        {"render.blur_behind", "視窗背後模糊（僅部分平台支援）", /*optional=*/true, /*default_available=*/false},
    };
    return kDecls;
}

}  // namespace

CapabilityMatrix::CapabilityMatrix(std::vector<CapabilityDecl> decls)
    : decls_(std::move(decls)) {}

CapabilityMatrix CapabilityMatrix::defaults() {
    return CapabilityMatrix(default_decls());
}

const CapabilityDecl* CapabilityMatrix::find(const CapabilityId& id) const {
    // 後定義者為準：反向掃描，讓重複 id 時最後一筆宣告勝出。
    for (auto it = decls_.rbegin(); it != decls_.rend(); ++it) {
        if (it->id == id) {
            return &(*it);
        }
    }
    return nullptr;
}

bool CapabilityMatrix::is_declared(const CapabilityId& id) const {
    return find(id) != nullptr;
}

bool CapabilityMatrix::has(const CapabilityId& id) const {
    const CapabilityDecl* d = find(id);
    // 保守：未宣告即不可用。相位 1 回傳宣告的預設可用性。
    return d != nullptr && d->default_available;
}

bool CapabilityMatrix::is_optional(const CapabilityId& id) const {
    const CapabilityDecl* d = find(id);
    return d != nullptr && d->optional;
}

}  // namespace ds::kernel
